#ifndef PLATFORM_LCD_H
#define PLATFORM_LCD_H

#include <stdbool.h>
#include <stdint.h>

#include "platform_controls.h"

typedef enum
{
    PLATFORM_UI_NORMAL,
    PLATFORM_UI_WEAPONS,
    PLATFORM_UI_CHEATS,
} platform_ui_mode_t;

void platform_lcd_present_rgb565(const uint16_t *framebuffer,
                                 int width,
                                 int height,
                                 int stride,
                                 const uint16_t *statusbar,
                                 int statusbar_height,
                                 bool statusbar_dirty);
void platform_lcd_init(void);
void platform_lcd_set_sound_muted(bool muted);
void platform_lcd_set_strafe_mode(bool enabled);
void platform_lcd_set_ui_mode(platform_ui_mode_t mode);
platform_ui_mode_t platform_lcd_get_ui_mode(void);
void platform_lcd_set_weapon_assets(const void *const *patches,
                                    uint16_t available_mask,
                                    const uint16_t *color_palette,
                                    const uint16_t *gray_palette);
void platform_lcd_set_weapon_state(uint16_t owned_mask,
                                   int selected_weapon,
                                   bool selector_enabled);
void platform_lcd_set_cheat_assets(const void *const *patches);
void platform_lcd_set_cheat_state(uint16_t active_mask);
void platform_lcd_invalidate_weapon_palette(void);

#endif
