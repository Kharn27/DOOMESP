#ifndef PLATFORM_LCD_H
#define PLATFORM_LCD_H

#include <stdbool.h>
#include <stdint.h>

void platform_lcd_present_rgb565(const uint16_t *framebuffer,
                                 int width,
                                 int height,
                                 int stride);
void platform_lcd_init(void);
void platform_lcd_set_sound_muted(bool muted);

#endif
