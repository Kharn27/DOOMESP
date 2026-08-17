#include "i_video.h"

#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"

#include "d_event.h"
#include "d_main.h"
#include "doomdef.h"
#include "v_video.h"

#include "platform/platform_input.h"
#include "platform/platform_lcd.h"

static uint16_t *rgb565_frame;
static uint16_t rgb565_palette[256];
static const char *TAG = "doom_video";

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

    while (platform_input_read(&event, &cmd))
    {
        D_PostEvent(&event);
    }
}

void I_UpdateNoBlit(void)
{
}

void I_FinishUpdate(void)
{
    int i;
    const byte *src = screens[0];

    for (i = 0; i < SCREENWIDTH * SCREENHEIGHT; ++i)
    {
        rgb565_frame[i] = rgb565_palette[src[i]];
    }

    platform_lcd_present_rgb565(rgb565_frame, SCREENWIDTH, SCREENHEIGHT, SCREENWIDTH);
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
    }
}
