#include "d_main.h"
#include "m_argv.h"

#include <stdlib.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_psram.h"

#include "platform/platform_fs.h"
#include "platform/platform_input.h"
#include "platform/platform_lcd.h"

static const char *TAG = "doom_app";
static char *doom_argv[] = { "doom", NULL };

static bool platform_init(void)
{
    if (!esp_psram_is_initialized())
    {
        ESP_LOGE(TAG, "PSRAM was not initialized by the bootloader");
        abort();
    }

    ESP_LOGI(TAG, "PSRAM: %u bytes (%u bytes free)",
             (unsigned)esp_psram_get_size(),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    platform_lcd_init();
    if (!platform_fs_init())
    {
        ESP_LOGE(TAG, "microSD unavailable; DOOM was not started. "
                      "Fix the card and press Reset to retry.");
        return false;
    }
    platform_input_init();
    return true;
}

void app_main(void)
{
    myargc = 1;
    myargv = doom_argv;
    if (!platform_init())
    {
        return;
    }
    D_DoomMain();
}
