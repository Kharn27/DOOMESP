#include "i_video.h"

#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"

#include "d_event.h"
#include "d_main.h"
#include "doomdef.h"
#include "doomstat.h"
#include "st_stuff.h"
#include "v_video.h"
#include "w_wad.h"

#include "platform/platform_input.h"
#include "platform/platform_lcd.h"

static uint16_t *rgb565_frame;
static uint16_t *rgb565_statusbar;
static uint16_t rgb565_palette[256];
static uint16_t rgb565_gray_palette[256];
static const void *weapon_patches[PLATFORM_WEAPON_COUNT];
static const void *cheat_patches[PLATFORM_CHEAT_COUNT];

// Selector artwork must outlive DOOM's purgeable WAD cache. Calling
// W_CacheLumpNum(..., PU_STATIC) is not enough: a later renderer request for
// the same sprite with PU_CACHE changes the existing block's tag, allowing it
// to be evicted while the touch UI still holds its address.
typedef struct
{
    int lump;
    void *data;
} ui_patch_cache_entry_t;

#define UI_PATCH_CACHE_CAPACITY \
    (PLATFORM_WEAPON_COUNT + PLATFORM_CHEAT_COUNT)

static ui_patch_cache_entry_t ui_patch_cache[UI_PATCH_CACHE_CAPACITY];
static int ui_patch_cache_count;
static size_t ui_patch_cache_bytes;
static uint16_t weapon_available_mask;
static byte *detached_status_screen;
static byte *detached_status_snapshot;
static byte *game_screen;
static bool detached_status_visible;
static bool detached_status_dirty = true;
static bool weapon_assets_ready;
static const char *TAG = "doom_video";

extern boolean menuactive;

static const void *load_stable_ui_patch(const char *name)
{
    const int lump = W_CheckNumForName((char *)name);
    if (lump < 0)
    {
        ESP_LOGW(TAG, "UI patch %s is not present in this IWAD", name);
        return NULL;
    }

    for (int entry = 0; entry < ui_patch_cache_count; ++entry)
    {
        if (ui_patch_cache[entry].lump == lump)
        {
            return ui_patch_cache[entry].data;
        }
    }

    if (ui_patch_cache_count >= UI_PATCH_CACHE_CAPACITY)
    {
        ESP_LOGE(TAG, "Stable UI patch cache is full");
        return NULL;
    }

    const int length = W_LumpLength(lump);
    if (length <= 0)
    {
        ESP_LOGW(TAG, "UI patch %s has an invalid size (%d)", name, length);
        return NULL;
    }

    void *data = heap_caps_malloc((size_t)length,
                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!data)
    {
        ESP_LOGE(TAG, "Unable to copy UI patch %s (%d bytes) to PSRAM",
                 name, length);
        return NULL;
    }

    W_ReadLump(lump, data);
    ui_patch_cache[ui_patch_cache_count].lump = lump;
    ui_patch_cache[ui_patch_cache_count].data = data;
    ++ui_patch_cache_count;
    ui_patch_cache_bytes += (size_t)length;
    return data;
}

static void initialize_weapon_assets(void)
{
    static const char *const lump_names[PLATFORM_WEAPON_COUNT] = {
        "PSTRA0", "CLIPA0", "SHOTA0",
        "MGUNA0", "LAUNA0", "PLASA0",
        "BFUGA0", "CSAWA0", "SGN2A0",
    };
    static const char *const cheat_lump_names[PLATFORM_CHEAT_COUNT] = {
        "SOULA0", "BPAKA0", "AMMOA0",
        "PINSA0", "PSTRA0", "PINVA0",
    };

    if (weapon_assets_ready)
    {
        return;
    }

    for (int weapon = 0; weapon < PLATFORM_WEAPON_COUNT; ++weapon)
    {
        weapon_patches[weapon] = load_stable_ui_patch(lump_names[weapon]);
        if (weapon_patches[weapon])
        {
            weapon_available_mask |= (uint16_t)(1u << weapon);
        }
    }

    platform_lcd_set_weapon_assets(weapon_patches,
                                   weapon_available_mask,
                                   rgb565_palette,
                                   rgb565_gray_palette);
    for (int cheat = 0; cheat < PLATFORM_CHEAT_COUNT; ++cheat)
    {
        cheat_patches[cheat] =
            load_stable_ui_patch(cheat_lump_names[cheat]);
    }
    platform_lcd_set_cheat_assets(cheat_patches);
    weapon_assets_ready = true;
    ESP_LOGI(TAG,
             "Selector assets ready (mask=0x%03x, %d unique patches, "
             "%u PSRAM bytes)",
             (unsigned)weapon_available_mask,
             ui_patch_cache_count,
             (unsigned)ui_patch_cache_bytes);
}

static void update_weapon_ui_state(void)
{
    uint16_t owned_mask = 0;
    uint16_t active_cheat_mask = 0;
    int selected_weapon = -1;
    const bool selector_enabled =
        gamestate == GS_LEVEL && usergame && !menuactive;

    if (gamestate == GS_LEVEL)
    {
        const player_t *player = &players[consoleplayer];
        for (int weapon = 0; weapon < NUMWEAPONS; ++weapon)
        {
            if (player->weaponowned[weapon])
            {
                owned_mask |= (uint16_t)(1u << weapon);
            }
        }
        selected_weapon = player->pendingweapon != wp_nochange
                              ? player->pendingweapon
                              : player->readyweapon;
        if (player->cheats & CF_GODMODE)
        {
            active_cheat_mask |= 1u << 0;
        }
        if (player->cheats & CF_NOCLIP)
        {
            active_cheat_mask |= 1u << 3;
        }
        if (player->powers[pw_strength])
        {
            active_cheat_mask |= 1u << 4;
        }
        if (player->powers[pw_invulnerability])
        {
            active_cheat_mask |= 1u << 5;
        }
    }

    platform_input_set_weapon_state(owned_mask,
                                    weapon_available_mask,
                                    selector_enabled);
    platform_lcd_set_weapon_state(owned_mask,
                                  selected_weapon,
                                  selector_enabled);
    platform_lcd_set_cheat_state(active_cheat_mask);
}

void I_InitGraphics(void)
{
    if (!rgb565_frame)
    {
        rgb565_frame = heap_caps_malloc(SCREENWIDTH * SCREENHEIGHT * sizeof(uint16_t),
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!rgb565_frame)
        {
            ESP_LOGE(TAG, "PSRAM framebuffer allocation failed");
            abort();
        }
    }

    if (!rgb565_statusbar)
    {
        rgb565_statusbar = heap_caps_malloc(SCREENWIDTH * ST_HEIGHT *
                                                sizeof(uint16_t),
                                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        detached_status_screen = heap_caps_malloc(SCREENWIDTH * SCREENHEIGHT,
                                                   MALLOC_CAP_SPIRAM |
                                                       MALLOC_CAP_8BIT);
        detached_status_snapshot = heap_caps_malloc(SCREENWIDTH * ST_HEIGHT,
                                                     MALLOC_CAP_SPIRAM |
                                                         MALLOC_CAP_8BIT);
        if (!rgb565_statusbar || !detached_status_screen ||
            !detached_status_snapshot)
        {
            ESP_LOGE(TAG, "Detached status-bar allocation failed");
            abort();
        }

        memset(detached_status_screen, 0, SCREENWIDTH * SCREENHEIGHT);
        memset(detached_status_snapshot, 0xff, SCREENWIDTH * ST_HEIGHT);
        ESP_LOGI(TAG, "Detached DOOM status bar ready (%dx%d)",
                 SCREENWIDTH, ST_HEIGHT);
        ESP_LOGI(TAG,
                 "Heap after video init: internal=%u bytes, PSRAM=%u bytes "
                 "(largest PSRAM block=%u)",
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL |
                                                    MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM |
                                                    MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM |
                                                            MALLOC_CAP_8BIT));
    }

    initialize_weapon_assets();
}

void I_ShutdownGraphics(void)
{
}

void I_StartFrame(void)
{
}

void I_StartTic(void)
{
    event_t event;
    ticcmd_t cmd;
    int requested_weapon;

    while (platform_input_read(&event, &cmd))
    {
        D_PostEvent(&event);
    }

    while (platform_input_take_weapon_request(&requested_weapon))
    {
        if (gamestate != GS_LEVEL || !usergame ||
            requested_weapon < 0 || requested_weapon >= NUMWEAPONS)
        {
            continue;
        }

        player_t *player = &players[consoleplayer];
        if (!player->weaponowned[requested_weapon] ||
            (gamemode == shareware &&
             (requested_weapon == wp_plasma ||
              requested_weapon == wp_bfg)) ||
            (gamemode != commercial &&
             requested_weapon == wp_supershotgun))
        {
            continue;
        }

        if (player->readyweapon != requested_weapon ||
            player->pendingweapon != wp_nochange)
        {
            player->pendingweapon = requested_weapon;
            ESP_LOGI(TAG, "Direct weapon request accepted: %d",
                     requested_weapon);
        }
    }
}

void I_UpdateNoBlit(void)
{
}

void I_BeginDetachedStatusBar(void)
{
    if (game_screen)
    {
        ESP_LOGE(TAG, "Nested detached status-bar render");
        abort();
    }

    game_screen = screens[0];
    screens[0] = detached_status_screen;
}

void I_EndDetachedStatusBar(void)
{
    const byte *status_pixels;

    if (!game_screen)
    {
        ESP_LOGE(TAG, "Detached status-bar render was not started");
        abort();
    }

    screens[0] = game_screen;
    game_screen = NULL;
    status_pixels = detached_status_screen + ST_Y * SCREENWIDTH;

    if (memcmp(detached_status_snapshot, status_pixels,
               SCREENWIDTH * ST_HEIGHT) != 0)
    {
        memcpy(detached_status_snapshot, status_pixels,
               SCREENWIDTH * ST_HEIGHT);
        detached_status_dirty = true;
    }

    if (!detached_status_visible)
    {
        detached_status_visible = true;
        detached_status_dirty = true;
    }
}

void I_HideDetachedStatusBar(void)
{
    if (detached_status_visible)
    {
        detached_status_visible = false;
        detached_status_dirty = true;
    }
}

void I_FinishUpdate(void)
{
    int i;
    const byte *src = screens[0];

    update_weapon_ui_state();

    for (i = 0; i < SCREENWIDTH * SCREENHEIGHT; ++i)
    {
        rgb565_frame[i] = rgb565_palette[src[i]];
    }

    if (detached_status_visible && detached_status_dirty)
    {
        for (i = 0; i < SCREENWIDTH * ST_HEIGHT; ++i)
        {
            rgb565_statusbar[i] = rgb565_palette[detached_status_snapshot[i]];
        }
    }

    platform_lcd_present_rgb565(rgb565_frame,
                                SCREENWIDTH, SCREENHEIGHT, SCREENWIDTH,
                                detached_status_visible
                                    ? rgb565_statusbar
                                    : NULL,
                                detached_status_visible ? ST_HEIGHT : 0,
                                detached_status_dirty);
    detached_status_dirty = false;
}

void I_ReadScreen(byte *scr)
{
    memcpy(scr, screens[0], SCREENWIDTH * SCREENHEIGHT);
}

void I_SetPalette(byte *palette)
{
    int i;

    for (i = 0; i < 256; ++i)
    {
        byte r = palette[i * 3 + 0];
        byte g = palette[i * 3 + 1];
        byte b = palette[i * 3 + 2];

        uint16_t color = (uint16_t)(((r & 0xF8) << 8)
                                    | ((g & 0xFC) << 3)
                                    | (b >> 3));

        // esp_lcd transmits the bytes in memory order, while the panel expects
        // the most significant RGB565 byte first.
        rgb565_palette[i] = (uint16_t)((color << 8) | (color >> 8));

        // Unowned weapon icons retain their shape but lose both saturation
        // and most of their brightness.
        const int luminance = (r * 77 + g * 150 + b * 29) >> 8;
        const byte gray = (byte)(18 + luminance * 45 / 100);
        color = (uint16_t)(((gray & 0xF8) << 8)
                           | ((gray & 0xFC) << 3)
                           | (gray >> 3));
        rgb565_gray_palette[i] =
            (uint16_t)((color << 8) | (color >> 8));
    }

    // Indexed status-bar pixels have not changed, but their RGB output has.
    detached_status_dirty = true;
    platform_lcd_invalidate_weapon_palette();
}
