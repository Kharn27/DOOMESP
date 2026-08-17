#include "i_sound.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driver/i2s_std.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "musplayer.h"
#include "platform_audio.h"
#include "platform_board.h"
#include "woody_opl_api.h"
#include "w_wad.h"
#include "z_zone.h"

#define AUDIO_FRAMES_PER_BUFFER 256
#define AUDIO_CHANNEL_COUNT 8
#define MUSIC_TICK_RATE 140
#define MUSIC_STATS_BUFFER_COUNT 128

typedef struct
{
    uint8_t *storage;
    const uint8_t *samples;
    uint32_t sample_count;
    uint32_t sample_rate;
} cached_sfx_t;

typedef struct
{
    bool active;
    int handle;
    int sfx_id;
    const uint8_t *samples;
    uint32_t sample_count;
    uint32_t position;
    uint32_t step;
    int gain;
    uint32_t start_order;
} mix_channel_t;

typedef struct
{
    // LittleMUS passes a pointer to this first member to adlib_write().
    musplayer_t player;
    const uint8_t *registered_song;
    uint32_t tick_phase;
    int registered_handle;
    int volume;
    bool bank_ready;
    bool playing;
    bool paused;
} music_state_t;

static const char *TAG = "doom_audio";

static i2s_chan_handle_t tx_channel;
static SemaphoreHandle_t mixer_mutex;
static TaskHandle_t audio_task_handle;
static volatile bool audio_task_running;
static bool audio_ready;
static bool sound_muted;

static cached_sfx_t sfx_cache[NUMSFX];
static mix_channel_t mix_channels[AUDIO_CHANNEL_COUNT];
static music_state_t music;
static uint32_t next_start_order;
static int next_handle = 1;
static int next_music_handle = 1;

void adlib_write(musplayer_t *player, int reg, int value)
{
    (void)player;
    woody_opl_write((uintptr_t)reg, (uint8_t)value);
}

static uint16_t read_u16_le(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t read_u32_le(const uint8_t *data)
{
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static int clamp_int(int value, int minimum, int maximum)
{
    if (value < minimum)
        return minimum;
    if (value > maximum)
        return maximum;
    return value;
}

static int linked_sfx_id(int id)
{
    int guard = 0;

    while (id > sfx_None && id < NUMSFX &&
           S_sfx[id].link && guard++ < NUMSFX)
    {
        id = (int)(S_sfx[id].link - S_sfx);
    }

    return id;
}

static cached_sfx_t *cache_sfx(int requested_id)
{
    int source_id = linked_sfx_id(requested_id);
    if (source_id <= sfx_None || source_id >= NUMSFX)
        return NULL;

    cached_sfx_t *cached = &sfx_cache[source_id];
    if (cached->samples)
        return cached;

    char lump_name[9];
    snprintf(lump_name, sizeof(lump_name), "ds%s", S_sfx[source_id].name);

    int lump = W_CheckNumForName(lump_name);
    if (lump < 0)
    {
        ESP_LOGW(TAG, "%s absent; using dspistol", lump_name);
        lump = W_CheckNumForName("dspistol");
    }
    if (lump < 0)
    {
        ESP_LOGE(TAG, "No fallback dspistol sound in the WAD");
        return NULL;
    }

    int lump_length = W_LumpLength(lump);
    if (lump_length <= 8)
    {
        ESP_LOGW(TAG, "Ignoring invalid sound lump %s (%d bytes)",
                 lump_name, lump_length);
        return NULL;
    }

    uint8_t *storage = heap_caps_malloc((size_t)lump_length,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!storage)
        storage = heap_caps_malloc((size_t)lump_length, MALLOC_CAP_8BIT);
    if (!storage)
    {
        ESP_LOGE(TAG, "Unable to allocate %d bytes for %s",
                 lump_length, lump_name);
        return NULL;
    }

    W_ReadLump(lump, storage);

    uint16_t format = read_u16_le(storage);
    uint32_t sample_rate = read_u16_le(storage + 2);
    uint32_t declared_count = read_u32_le(storage + 4);
    uint32_t available_count = (uint32_t)lump_length - 8U;

    if (format != 3)
    {
        ESP_LOGW(TAG, "Ignoring unsupported sound %s (DMX format %u)",
                 lump_name, (unsigned)format);
        heap_caps_free(storage);
        return NULL;
    }

    if (declared_count == 0 || declared_count > available_count)
        declared_count = available_count;
    if (sample_rate < 4000 || sample_rate > 48000)
        sample_rate = 11025;

    cached->storage = storage;
    cached->samples = storage + 8;
    cached->sample_count = declared_count;
    cached->sample_rate = sample_rate;
    S_sfx[source_id].lumpnum = lump;

    ESP_LOGI(TAG, "Loaded %s: %u samples at %u Hz",
             lump_name, (unsigned)declared_count, (unsigned)sample_rate);
    return cached;
}

static uint32_t playback_step(const cached_sfx_t *sfx, int pitch)
{
    pitch = clamp_int(pitch, 0, 255);
    double pitch_ratio = pow(2.0, ((double)pitch - 128.0) / 64.0);
    double step = ((double)sfx->sample_rate * pitch_ratio * 65536.0) /
                  (double)PLATFORM_AUDIO_SAMPLE_RATE;

    if (step < 1.0)
        step = 1.0;
    if (step > 4294967295.0)
        step = 4294967295.0;
    return (uint32_t)step;
}

static int mono_gain(int volume, int separation)
{
    // The menu stores volume as 0..15, while the Linux mixer expected 0..127.
    if (volume <= 15)
        volume *= 8;
    volume = clamp_int(volume, 0, 127);
    separation = clamp_int(separation, 0, 255) + 1;

    int left = volume -
               ((volume * separation * separation) >> 16);
    separation -= 257;
    int right = volume -
                ((volume * separation * separation) >> 16);

    return clamp_int((left + right) / 2, 0, 127);
}

static bool is_singular_sfx(int id)
{
    return id == sfx_sawup || id == sfx_sawidl ||
           id == sfx_sawful || id == sfx_sawhit ||
           id == sfx_stnmov || id == sfx_pistol;
}

static bool fill_music_buffer(int16_t *output)
{
    memset(output, 0,
           AUDIO_FRAMES_PER_BUFFER * 2 * sizeof(output[0]));

    if (!music.bank_ready || !music.playing || music.paused)
        return false;

    int frame = 0;
    while (frame < AUDIO_FRAMES_PER_BUFFER && music.playing)
    {
        while (music.tick_phase >= PLATFORM_AUDIO_SAMPLE_RATE)
        {
            music.tick_phase -= PLATFORM_AUDIO_SAMPLE_RATE;
            if (!musplay_tick(&music.player))
            {
                music.playing = false;
                break;
            }
        }
        if (!music.playing)
            break;

        uint32_t phase_remaining =
            PLATFORM_AUDIO_SAMPLE_RATE - music.tick_phase;
        int frames_to_tick =
            (int)((phase_remaining + MUSIC_TICK_RATE - 1) /
                  MUSIC_TICK_RATE);
        int chunk = AUDIO_FRAMES_PER_BUFFER - frame;
        if (chunk > frames_to_tick)
            chunk = frames_to_tick;

        woody_opl_getsample(output + frame * 2, chunk);
        music.tick_phase += (uint32_t)chunk * MUSIC_TICK_RATE;
        frame += chunk;
    }

    return true;
}

static bool fill_mix_buffer(int16_t *output, int16_t *music_output)
{
    if (xSemaphoreTake(mixer_mutex, portMAX_DELAY) != pdTRUE)
    {
        memset(output, 0,
               AUDIO_FRAMES_PER_BUFFER * 2 * sizeof(output[0]));
        return false;
    }

    bool music_active = fill_music_buffer(music_output);

    for (int frame = 0; frame < AUDIO_FRAMES_PER_BUFFER; ++frame)
    {
        int32_t mixed = 0;

        for (int channel_index = 0;
             channel_index < AUDIO_CHANNEL_COUNT;
             ++channel_index)
        {
            mix_channel_t *channel = &mix_channels[channel_index];
            if (!channel->active)
                continue;

            uint32_t sample_index = channel->position >> 16;
            if (sample_index >= channel->sample_count)
            {
                channel->active = false;
                continue;
            }

            int sample = (int)channel->samples[sample_index] - 128;
            mixed += sample * channel->gain * 2;
            channel->position += channel->step;

            if ((channel->position >> 16) >= channel->sample_count)
                channel->active = false;
        }

        int32_t music_mono =
            ((int32_t)music_output[frame * 2] +
             (int32_t)music_output[frame * 2 + 1]) /
            2;
        mixed += (music_mono * music.volume) / 127;

        if (sound_muted)
            mixed = 0;
        mixed = clamp_int(mixed, -32768, 32767);
        output[frame * 2] = (int16_t)mixed;
        output[frame * 2 + 1] = (int16_t)mixed;
    }

    xSemaphoreGive(mixer_mutex);
    return music_active;
}

static void audio_task(void *argument)
{
    (void)argument;
    static int16_t output[AUDIO_FRAMES_PER_BUFFER * 2];
    static int16_t music_output[AUDIO_FRAMES_PER_BUFFER * 2];
    uint32_t stats_buffers = 0;
    int64_t stats_total_us = 0;
    int64_t stats_max_us = 0;

    while (audio_task_running)
    {
        int64_t started_us = esp_timer_get_time();
        bool music_active = fill_mix_buffer(output, music_output);
        int64_t elapsed_us = esp_timer_get_time() - started_us;

        if (music_active && stats_buffers < MUSIC_STATS_BUFFER_COUNT)
        {
            ++stats_buffers;
            stats_total_us += elapsed_us;
            if (elapsed_us > stats_max_us)
                stats_max_us = elapsed_us;

            if (stats_buffers == MUSIC_STATS_BUFFER_COUNT)
            {
                ESP_LOGI(TAG,
                         "Music mixer CPU: avg=%lld us max=%lld us "
                         "per %d us buffer",
                         (long long)(stats_total_us / stats_buffers),
                         (long long)stats_max_us,
                         AUDIO_FRAMES_PER_BUFFER * 1000000 /
                             PLATFORM_AUDIO_SAMPLE_RATE);
            }
        }

        size_t written = 0;
        esp_err_t err = i2s_channel_write(tx_channel,
                                          output,
                                          sizeof(output),
                                          &written,
                                          100);
        if (err != ESP_OK && err != ESP_ERR_TIMEOUT)
            ESP_LOGW(TAG, "I2S write failed: %s", esp_err_to_name(err));
    }

    audio_task_handle = NULL;
    vTaskDelete(NULL);
}

static bool init_i2s(void)
{
    i2s_chan_config_t channel_config =
        I2S_CHANNEL_DEFAULT_CONFIG(PLATFORM_AUDIO_I2S_PORT,
                                   I2S_ROLE_MASTER);
    channel_config.dma_desc_num = 6;
    channel_config.dma_frame_num = AUDIO_FRAMES_PER_BUFFER;
    channel_config.auto_clear = true;

    esp_err_t err = i2s_new_channel(&channel_config, &tx_channel, NULL);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Unable to create I2S channel: %s",
                 esp_err_to_name(err));
        return false;
    }

    i2s_std_config_t standard_config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(PLATFORM_AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = PLATFORM_AUDIO_GPIO_BCLK,
            .ws = PLATFORM_AUDIO_GPIO_LRCLK,
            .dout = PLATFORM_AUDIO_GPIO_DATA,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    err = i2s_channel_init_std_mode(tx_channel, &standard_config);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Unable to configure I2S: %s", esp_err_to_name(err));
        i2s_del_channel(tx_channel);
        tx_channel = NULL;
        return false;
    }

    err = i2s_channel_enable(tx_channel);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Unable to enable I2S: %s", esp_err_to_name(err));
        i2s_del_channel(tx_channel);
        tx_channel = NULL;
        return false;
    }

    return true;
}

static void init_music_bank(void)
{
    memset(&music, 0, sizeof(music));
    music.volume = 64;

    int lump = W_CheckNumForName("GENMIDI");
    if (lump < 0)
    {
        ESP_LOGW(TAG, "GENMIDI is missing; music disabled");
        return;
    }

    int length = W_LumpLength(lump);
    if (length < 8 + 175 * 36)
    {
        ESP_LOGW(TAG, "GENMIDI is too short (%d bytes); music disabled",
                 length);
        return;
    }

    uint8_t *bank = W_CacheLumpNum(lump, PU_STATIC);
    if (memcmp(bank, "#OPL_II#", 8) != 0)
    {
        ESP_LOGW(TAG, "GENMIDI has an invalid header; music disabled");
        Z_Free(bank);
        return;
    }

    musplay_op2bank(&music.player, (char *)(bank + 8));
    Z_Free(bank);
    music.bank_ready = true;
    ESP_LOGI(TAG, "GENMIDI OPL instrument bank ready");
}

void I_InitSound(void)
{
    memset(sfx_cache, 0, sizeof(sfx_cache));
    memset(mix_channels, 0, sizeof(mix_channels));
    sound_muted = false;
    init_music_bank();

    mixer_mutex = xSemaphoreCreateMutex();
    if (!mixer_mutex)
    {
        ESP_LOGE(TAG, "Unable to create audio mixer mutex");
        return;
    }

    if (!init_i2s())
        return;

    audio_task_running = true;
    BaseType_t created = xTaskCreatePinnedToCore(audio_task,
                                                 "doom_audio",
                                                 8192,
                                                 NULL,
                                                 6,
                                                 &audio_task_handle,
                                                 tskNO_AFFINITY);
    if (created != pdPASS)
    {
        ESP_LOGE(TAG, "Unable to create audio task");
        audio_task_running = false;
        i2s_channel_disable(tx_channel);
        i2s_del_channel(tx_channel);
        tx_channel = NULL;
        return;
    }

    audio_ready = true;
    ESP_LOGI(TAG,
             "NS4168 ready: %d Hz, BCLK=%d LRCLK=%d DATA=%d",
             PLATFORM_AUDIO_SAMPLE_RATE,
             PLATFORM_AUDIO_GPIO_BCLK,
             PLATFORM_AUDIO_GPIO_LRCLK,
             PLATFORM_AUDIO_GPIO_DATA);
}

bool platform_audio_toggle_mute(void)
{
    if (!mixer_mutex)
    {
        sound_muted = !sound_muted;
        return sound_muted;
    }

    xSemaphoreTake(mixer_mutex, portMAX_DELAY);
    sound_muted = !sound_muted;
    const bool muted = sound_muted;
    xSemaphoreGive(mixer_mutex);

    ESP_LOGI(TAG, "Audio %s", muted ? "muted" : "enabled");
    return muted;
}

void I_UpdateSound(void)
{
    // Mixing is performed continuously by audio_task().
}

void I_SubmitSound(void)
{
    // I2S DMA is fed continuously by audio_task().
}

void I_ShutdownSound(void)
{
    if (!audio_ready)
        return;

    audio_task_running = false;
    for (int attempt = 0;
         audio_task_handle && attempt < 30;
         ++attempt)
    {
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    if (audio_task_handle)
    {
        vTaskDelete(audio_task_handle);
        audio_task_handle = NULL;
    }

    i2s_channel_disable(tx_channel);
    i2s_del_channel(tx_channel);
    tx_channel = NULL;
    audio_ready = false;
}

void I_SetChannels(void)
{
    if (!mixer_mutex)
        return;

    xSemaphoreTake(mixer_mutex, portMAX_DELAY);
    memset(mix_channels, 0, sizeof(mix_channels));
    xSemaphoreGive(mixer_mutex);
}

int I_GetSfxLumpNum(sfxinfo_t *sfxinfo)
{
    char name[9];
    snprintf(name, sizeof(name), "ds%s", sfxinfo->name);
    return W_GetNumForName(name);
}

int I_StartSound(int id, int vol, int sep, int pitch, int priority)
{
    (void)priority;
    if (!audio_ready || id <= sfx_None || id >= NUMSFX)
        return 0;

    cached_sfx_t *cached = cache_sfx(id);
    if (!cached)
        return 0;

    int source_id = linked_sfx_id(id);

    xSemaphoreTake(mixer_mutex, portMAX_DELAY);

    if (is_singular_sfx(source_id))
    {
        for (int i = 0; i < AUDIO_CHANNEL_COUNT; ++i)
        {
            if (mix_channels[i].active &&
                mix_channels[i].sfx_id == source_id)
            {
                mix_channels[i].active = false;
            }
        }
    }

    int slot = -1;
    uint32_t oldest_order = UINT32_MAX;
    for (int i = 0; i < AUDIO_CHANNEL_COUNT; ++i)
    {
        if (!mix_channels[i].active)
        {
            slot = i;
            break;
        }
        if (mix_channels[i].start_order < oldest_order)
        {
            oldest_order = mix_channels[i].start_order;
            slot = i;
        }
    }

    int handle = next_handle++;
    if (next_handle <= 0)
        next_handle = 1;

    mix_channels[slot] = (mix_channel_t){
        .active = true,
        .handle = handle,
        .sfx_id = source_id,
        .samples = cached->samples,
        .sample_count = cached->sample_count,
        .position = 0,
        .step = playback_step(cached, pitch),
        .gain = mono_gain(vol, sep),
        .start_order = ++next_start_order,
    };

    xSemaphoreGive(mixer_mutex);
    return handle;
}

void I_StopSound(int handle)
{
    if (!mixer_mutex || handle <= 0)
        return;

    xSemaphoreTake(mixer_mutex, portMAX_DELAY);
    for (int i = 0; i < AUDIO_CHANNEL_COUNT; ++i)
    {
        if (mix_channels[i].active && mix_channels[i].handle == handle)
        {
            mix_channels[i].active = false;
            break;
        }
    }
    xSemaphoreGive(mixer_mutex);
}

int I_SoundIsPlaying(int handle)
{
    if (!mixer_mutex || handle <= 0)
        return 0;

    int playing = 0;
    xSemaphoreTake(mixer_mutex, portMAX_DELAY);
    for (int i = 0; i < AUDIO_CHANNEL_COUNT; ++i)
    {
        if (mix_channels[i].active && mix_channels[i].handle == handle)
        {
            playing = 1;
            break;
        }
    }
    xSemaphoreGive(mixer_mutex);
    return playing;
}

void I_UpdateSoundParams(int handle, int vol, int sep, int pitch)
{
    if (!mixer_mutex || handle <= 0)
        return;

    xSemaphoreTake(mixer_mutex, portMAX_DELAY);
    for (int i = 0; i < AUDIO_CHANNEL_COUNT; ++i)
    {
        mix_channel_t *channel = &mix_channels[i];
        if (!channel->active || channel->handle != handle)
            continue;

        cached_sfx_t *cached = &sfx_cache[channel->sfx_id];
        channel->gain = mono_gain(vol, sep);
        channel->step = playback_step(cached, pitch);
        break;
    }
    xSemaphoreGive(mixer_mutex);
}

void I_InitMusic(void)
{
}

void I_ShutdownMusic(void)
{
    if (mixer_mutex)
    {
        xSemaphoreTake(mixer_mutex, portMAX_DELAY);
        if (music.playing)
            musplay_stop(&music.player);
        music.playing = false;
        music.registered_song = NULL;
        xSemaphoreGive(mixer_mutex);
    }
}

void I_SetMusicVolume(int volume)
{
    if (volume <= 15)
        volume *= 8;
    volume = clamp_int(volume, 0, 127);

    if (!mixer_mutex)
    {
        music.volume = volume;
        return;
    }

    xSemaphoreTake(mixer_mutex, portMAX_DELAY);
    music.volume = volume;
    xSemaphoreGive(mixer_mutex);
}

void I_PauseSong(int handle)
{
    if (!mixer_mutex || handle != music.registered_handle)
        return;

    xSemaphoreTake(mixer_mutex, portMAX_DELAY);
    music.paused = true;
    xSemaphoreGive(mixer_mutex);
}

void I_ResumeSong(int handle)
{
    if (!mixer_mutex || handle != music.registered_handle)
        return;

    xSemaphoreTake(mixer_mutex, portMAX_DELAY);
    music.paused = false;
    xSemaphoreGive(mixer_mutex);
}

int I_RegisterSong(void *data)
{
    if (!music.bank_ready || !data ||
        memcmp(data, "MUS\x1a", 4) != 0)
    {
        ESP_LOGW(TAG, "Ignoring invalid or unsupported music lump");
        return 0;
    }

    xSemaphoreTake(mixer_mutex, portMAX_DELAY);
    music.registered_song = data;
    music.registered_handle = next_music_handle++;
    if (next_music_handle <= 0)
        next_music_handle = 1;
    int handle = music.registered_handle;
    xSemaphoreGive(mixer_mutex);
    return handle;
}

void I_PlaySong(int handle, int looping)
{
    if (!mixer_mutex || !music.bank_ready ||
        handle <= 0 || handle != music.registered_handle ||
        !music.registered_song)
    {
        return;
    }

    xSemaphoreTake(mixer_mutex, portMAX_DELAY);
    woody_opl_init(PLATFORM_AUDIO_SAMPLE_RATE);
    musplay_volume(&music.player, 100);
    musplay_start(&music.player,
                  (char *)music.registered_song,
                  looping != 0);
    music.tick_phase = PLATFORM_AUDIO_SAMPLE_RATE;
    music.paused = false;
    music.playing = true;
    xSemaphoreGive(mixer_mutex);

    ESP_LOGI(TAG, "MUS playback started (loop=%d)", looping != 0);
}

void I_StopSong(int handle)
{
    if (!mixer_mutex || handle != music.registered_handle)
        return;

    xSemaphoreTake(mixer_mutex, portMAX_DELAY);
    if (music.playing)
        musplay_stop(&music.player);
    music.playing = false;
    music.paused = false;
    xSemaphoreGive(mixer_mutex);
}

void I_UnRegisterSong(int handle)
{
    if (!mixer_mutex || handle != music.registered_handle)
        return;

    xSemaphoreTake(mixer_mutex, portMAX_DELAY);
    music.registered_song = NULL;
    music.registered_handle = 0;
    xSemaphoreGive(mixer_mutex);
}
