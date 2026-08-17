#include "platform_lcd.h"

#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_axs15231b.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"

#include "m_swap.h"
#include "platform_controls.h"
#include "r_defs.h"

#define LCD_SPI_HOST SPI2_HOST

#define LCD_NATIVE_H_RES PLATFORM_SCREEN_WIDTH
#define LCD_NATIVE_V_RES PLATFORM_SCREEN_HEIGHT
#define LCD_GAME_WIDTH PLATFORM_SCREEN_WIDTH
#define LCD_GAME_HEIGHT PLATFORM_GAME_HEIGHT

#define LCD_GPIO_CS 45
#define LCD_GPIO_CLK 47
#define LCD_GPIO_D0 21
#define LCD_GPIO_D1 48
#define LCD_GPIO_D2 40
#define LCD_GPIO_D3 39
#define LCD_GPIO_RST GPIO_NUM_NC
#define LCD_GPIO_BL 1

#define LCD_PCLK_HZ (40 * 1000 * 1000)
#define LCD_TRANSFER_LINES 20
#define LCD_TRANSFER_PIXELS (LCD_NATIVE_H_RES * LCD_TRANSFER_LINES)
#define LCD_TRANSFER_BYTES (LCD_TRANSFER_PIXELS * sizeof(uint16_t))
#define LCD_CONTROLS_LINES (LCD_NATIVE_V_RES - PLATFORM_LOWER_TOP)
#define LCD_CONTROLS_PIXELS (LCD_NATIVE_H_RES * LCD_CONTROLS_LINES)
#define LCD_CONTROLS_BYTES (LCD_CONTROLS_PIXELS * sizeof(uint16_t))

static const char *TAG = "platform_lcd";
static esp_lcd_panel_io_handle_t lcd_io;
static esp_lcd_panel_handle_t lcd_panel;
static uint16_t *transfer_buffers[2];
static uint16_t *controls_frame;
static volatile bool sound_muted;
static volatile bool sound_button_dirty;
static volatile bool strafe_mode;
static volatile platform_ui_mode_t ui_mode = PLATFORM_UI_NORMAL;
static volatile bool panel_dirty;
static const void *weapon_patches[PLATFORM_WEAPON_COUNT];
static const void *cheat_patches[PLATFORM_CHEAT_COUNT];
static const uint16_t *weapon_color_palette;
static const uint16_t *weapon_gray_palette;
static uint16_t weapon_available_mask;
static uint16_t weapon_owned_mask;
static int weapon_selected = -1;
static bool weapon_selector_enabled;
static uint16_t cheat_active_mask;

// 320x480 gate/source timings used by Espressif's AXS15231B QSPI test and
// by the known-working Arduino_GFX driver for the JC3248W535.  The generic
// component defaults target a different panel timing and corrupt this LCD's
// raster even though commands and pixel data are accepted.
static const axs15231b_lcd_init_cmd_t jc3248w535_init_commands[] = {
    {0xBB, (uint8_t[]){0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5A, 0xA5}, 8, 0},
    {0xA0, (uint8_t[]){0xC0, 0x10, 0x00, 0x02, 0x00, 0x00, 0x04, 0x3F, 0x20, 0x05, 0x3F, 0x3F, 0x00, 0x00, 0x00, 0x00, 0x00}, 17, 0},
    {0xA2, (uint8_t[]){0x30, 0x3C, 0x24, 0x14, 0xD0, 0x20, 0xFF, 0xE0, 0x40, 0x19, 0x80, 0x80, 0x80, 0x20, 0xF9, 0x10, 0x02, 0xFF, 0xFF, 0xF0, 0x90, 0x01, 0x32, 0xA0, 0x91, 0xE0, 0x20, 0x7F, 0xFF, 0x00, 0x5A}, 31, 0},
    {0xD0, (uint8_t[]){0xE0, 0x40, 0x51, 0x24, 0x08, 0x05, 0x10, 0x01, 0x20, 0x15, 0x42, 0xC2, 0x22, 0x22, 0xAA, 0x03, 0x10, 0x12, 0x60, 0x14, 0x1E, 0x51, 0x15, 0x00, 0x8A, 0x20, 0x00, 0x03, 0x3A, 0x12}, 30, 0},
    {0xA3, (uint8_t[]){0xA0, 0x06, 0xAA, 0x00, 0x08, 0x02, 0x0A, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x55, 0x55}, 22, 0},
    {0xC1, (uint8_t[]){0x31, 0x04, 0x02, 0x02, 0x71, 0x05, 0x24, 0x55, 0x02, 0x00, 0x41, 0x00, 0x53, 0xFF, 0xFF, 0xFF, 0x4F, 0x52, 0x00, 0x4F, 0x52, 0x00, 0x45, 0x3B, 0x0B, 0x02, 0x0D, 0x00, 0xFF, 0x40}, 30, 0},
    {0xC3, (uint8_t[]){0x00, 0x00, 0x00, 0x50, 0x03, 0x00, 0x00, 0x00, 0x01, 0x80, 0x01}, 11, 0},
    {0xC4, (uint8_t[]){0x00, 0x24, 0x33, 0x80, 0x00, 0xEA, 0x64, 0x32, 0xC8, 0x64, 0xC8, 0x32, 0x90, 0x90, 0x11, 0x06, 0xDC, 0xFA, 0x00, 0x00, 0x80, 0xFE, 0x10, 0x10, 0x00, 0x0A, 0x0A, 0x44, 0x50}, 29, 0},
    {0xC5, (uint8_t[]){0x18, 0x00, 0x00, 0x03, 0xFE, 0x3A, 0x4A, 0x20, 0x30, 0x10, 0x88, 0xDE, 0x0D, 0x08, 0x0F, 0x0F, 0x01, 0x3A, 0x4A, 0x20, 0x10, 0x10, 0x00}, 23, 0},
    {0xC6, (uint8_t[]){0x05, 0x0A, 0x05, 0x0A, 0x00, 0xE0, 0x2E, 0x0B, 0x12, 0x22, 0x12, 0x22, 0x01, 0x03, 0x00, 0x3F, 0x6A, 0x18, 0xC8, 0x22}, 20, 0},
    {0xC7, (uint8_t[]){0x50, 0x32, 0x28, 0x00, 0xA2, 0x80, 0x8F, 0x00, 0x80, 0xFF, 0x07, 0x11, 0x9C, 0x67, 0xFF, 0x24, 0x0C, 0x0D, 0x0E, 0x0F}, 20, 0},
    {0xC9, (uint8_t[]){0x33, 0x44, 0x44, 0x01}, 4, 0},
    {0xCF, (uint8_t[]){0x2C, 0x1E, 0x88, 0x58, 0x13, 0x18, 0x56, 0x18, 0x1E, 0x68, 0x88, 0x00, 0x65, 0x09, 0x22, 0xC4, 0x0C, 0x77, 0x22, 0x44, 0xAA, 0x55, 0x08, 0x08, 0x12, 0xA0, 0x08}, 27, 0},
    {0xD5, (uint8_t[]){0x40, 0x8E, 0x8D, 0x01, 0x35, 0x04, 0x92, 0x74, 0x04, 0x92, 0x74, 0x04, 0x08, 0x6A, 0x04, 0x46, 0x03, 0x03, 0x03, 0x03, 0x82, 0x01, 0x03, 0x00, 0xE0, 0x51, 0xA1, 0x00, 0x00, 0x00}, 30, 0},
    {0xD6, (uint8_t[]){0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE, 0x93, 0x00, 0x01, 0x83, 0x07, 0x07, 0x00, 0x07, 0x07, 0x00, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x00, 0x84, 0x00, 0x20, 0x01, 0x00}, 30, 0},
    {0xD7, (uint8_t[]){0x03, 0x01, 0x0B, 0x09, 0x0F, 0x0D, 0x1E, 0x1F, 0x18, 0x1D, 0x1F, 0x19, 0x40, 0x8E, 0x04, 0x00, 0x20, 0xA0, 0x1F}, 19, 0},
    {0xD8, (uint8_t[]){0x02, 0x00, 0x0A, 0x08, 0x0E, 0x0C, 0x1E, 0x1F, 0x18, 0x1D, 0x1F, 0x19}, 12, 0},
    {0xD9, (uint8_t[]){0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F}, 12, 0},
    {0xDD, (uint8_t[]){0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F}, 12, 0},
    {0xDF, (uint8_t[]){0x44, 0x73, 0x4B, 0x69, 0x00, 0x0A, 0x02, 0x90}, 8, 0},
    {0xE0, (uint8_t[]){0x3B, 0x28, 0x10, 0x16, 0x0C, 0x06, 0x11, 0x28, 0x5C, 0x21, 0x0D, 0x35, 0x13, 0x2C, 0x33, 0x28, 0x0D}, 17, 0},
    {0xE1, (uint8_t[]){0x37, 0x28, 0x10, 0x16, 0x0B, 0x06, 0x11, 0x28, 0x5C, 0x21, 0x0D, 0x35, 0x14, 0x2C, 0x33, 0x28, 0x0F}, 17, 0},
    {0xE2, (uint8_t[]){0x3B, 0x07, 0x12, 0x18, 0x0E, 0x0D, 0x17, 0x35, 0x44, 0x32, 0x0C, 0x14, 0x14, 0x36, 0x3A, 0x2F, 0x0D}, 17, 0},
    {0xE3, (uint8_t[]){0x37, 0x07, 0x12, 0x18, 0x0E, 0x0D, 0x17, 0x35, 0x44, 0x32, 0x0C, 0x14, 0x14, 0x36, 0x32, 0x2F, 0x0F}, 17, 0},
    {0xE4, (uint8_t[]){0x3B, 0x07, 0x12, 0x18, 0x0E, 0x0D, 0x17, 0x39, 0x44, 0x2E, 0x0C, 0x14, 0x14, 0x36, 0x3A, 0x2F, 0x0D}, 17, 0},
    {0xE5, (uint8_t[]){0x37, 0x07, 0x12, 0x18, 0x0E, 0x0D, 0x17, 0x39, 0x44, 0x2E, 0x0C, 0x14, 0x14, 0x36, 0x3A, 0x2F, 0x0F}, 17, 0},
    {0xA4, (uint8_t[]){0x85, 0x85, 0x95, 0x82, 0xAF, 0xAA, 0xAA, 0x80, 0x10, 0x30, 0x40, 0x40, 0x20, 0xFF, 0x60, 0x30}, 16, 0},
    {0xA4, (uint8_t[]){0x85, 0x85, 0x95, 0x85}, 4, 0},
    {0xBB, (uint8_t[]){0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 8, 0},
    {0x13, NULL, 0, 0},
    {0x11, NULL, 0, 120},
    {0x2C, (uint8_t[]){0x00, 0x00, 0x00, 0x00}, 4, 0},
};

static void draw_native_strip(int y, const uint16_t *pixels)
{
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(lcd_panel,
                                              0, y,
                                              LCD_NATIVE_H_RES,
                                              y + LCD_TRANSFER_LINES,
                                              pixels));
}

static uint16_t rgb565_be(uint8_t red, uint8_t green, uint8_t blue)
{
    const uint16_t color = (uint16_t)(((red & 0xf8) << 8) |
                                      ((green & 0xfc) << 3) |
                                      (blue >> 3));
    return (uint16_t)((color << 8) | (color >> 8));
}

static void fill_rectangle(uint16_t *strip, int strip_y,
                           int x, int y, int width, int height,
                           uint16_t color)
{
    int x0 = x < 0 ? 0 : x;
    int y0 = y < strip_y ? strip_y : y;
    int x1 = x + width > LCD_NATIVE_H_RES ? LCD_NATIVE_H_RES : x + width;
    int y1 = y + height > strip_y + LCD_TRANSFER_LINES
                 ? strip_y + LCD_TRANSFER_LINES
                 : y + height;

    if (x0 >= x1 || y0 >= y1)
    {
        return;
    }

    for (int absolute_y = y0; absolute_y < y1; ++absolute_y)
    {
        uint16_t *row = strip + (absolute_y - strip_y) * LCD_NATIVE_H_RES;
        for (int absolute_x = x0; absolute_x < x1; ++absolute_x)
        {
            row[absolute_x] = color;
        }
    }
}

typedef struct
{
    int x;
    int y;
} draw_point_t;

// Small convex polygons are enough for the industrial control-panel shapes.
// Rendering is clipped to the current DMA strip just like the other helpers.
static void fill_convex_polygon(uint16_t *strip, int strip_y,
                                const draw_point_t *points, int point_count,
                                uint16_t color)
{
    int polygon_y0 = points[0].y;
    int polygon_y1 = points[0].y;
    for (int point = 1; point < point_count; ++point)
    {
        if (points[point].y < polygon_y0)
        {
            polygon_y0 = points[point].y;
        }
        if (points[point].y > polygon_y1)
        {
            polygon_y1 = points[point].y;
        }
    }

    const int y0 = polygon_y0 > strip_y ? polygon_y0 : strip_y;
    const int y1 = polygon_y1 + 1 < strip_y + LCD_TRANSFER_LINES
                       ? polygon_y1 + 1
                       : strip_y + LCD_TRANSFER_LINES;

    for (int y = y0; y < y1; ++y)
    {
        int left = LCD_NATIVE_H_RES;
        int right = -1;

        for (int point = 0; point < point_count; ++point)
        {
            const draw_point_t start = points[point];
            const draw_point_t end = points[(point + 1) % point_count];
            const int edge_y0 = start.y < end.y ? start.y : end.y;
            const int edge_y1 = start.y > end.y ? start.y : end.y;

            // Use a half-open edge interval so shared vertices are counted
            // once. Horizontal edges are covered by their adjacent edges.
            if (start.y == end.y || y < edge_y0 || y >= edge_y1)
            {
                continue;
            }

            const int x = start.x +
                          (y - start.y) * (end.x - start.x) /
                              (end.y - start.y);
            if (x < left)
            {
                left = x;
            }
            if (x > right)
            {
                right = x;
            }
        }

        if (left <= right)
        {
            if (left < 0)
            {
                left = 0;
            }
            if (right >= LCD_NATIVE_H_RES)
            {
                right = LCD_NATIVE_H_RES - 1;
            }
            uint16_t *row = strip + (y - strip_y) * LCD_NATIVE_H_RES;
            for (int x = left; x <= right; ++x)
            {
                row[x] = color;
            }
        }
    }
}

static void fill_octagon(uint16_t *strip, int strip_y,
                         int center_x, int center_y, int radius, int corner,
                         uint16_t color)
{
    const draw_point_t points[] = {
        {center_x - radius + corner, center_y - radius},
        {center_x + radius - corner, center_y - radius},
        {center_x + radius, center_y - radius + corner},
        {center_x + radius, center_y + radius - corner},
        {center_x + radius - corner, center_y + radius},
        {center_x - radius + corner, center_y + radius},
        {center_x - radius, center_y + radius - corner},
        {center_x - radius, center_y - radius + corner},
    };
    fill_convex_polygon(strip, strip_y, points,
                        sizeof(points) / sizeof(points[0]), color);
}

static void fill_chamfered_rectangle(uint16_t *strip, int strip_y,
                                     int x, int y, int width, int height,
                                     int corner, uint16_t color)
{
    const draw_point_t points[] = {
        {x + corner, y},
        {x + width - corner - 1, y},
        {x + width - 1, y + corner},
        {x + width - 1, y + height - corner - 1},
        {x + width - corner - 1, y + height - 1},
        {x + corner, y + height - 1},
        {x, y + height - corner - 1},
        {x, y + corner},
    };
    fill_convex_polygon(strip, strip_y, points,
                        sizeof(points) / sizeof(points[0]), color);
}

static const uint8_t *glyph_rows(char character)
{
    static const uint8_t glyph_a[] = {0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11};
    static const uint8_t glyph_b[] = {0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e};
    static const uint8_t glyph_c[] = {0x0f, 0x10, 0x10, 0x10, 0x10, 0x10, 0x0f};
    static const uint8_t glyph_d[] = {0x1e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1e};
    static const uint8_t glyph_f[] = {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10};
    static const uint8_t glyph_g[] = {0x0f, 0x10, 0x10, 0x17, 0x11, 0x11, 0x0f};
    static const uint8_t glyph_h[] = {0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11};
    static const uint8_t glyph_i[] = {0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1f};
    static const uint8_t glyph_k[] = {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11};
    static const uint8_t glyph_l[] = {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f};
    static const uint8_t glyph_r[] = {0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11};
    static const uint8_t glyph_e[] = {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f};
    static const uint8_t glyph_p[] = {0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10};
    static const uint8_t glyph_u[] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e};
    static const uint8_t glyph_s[] = {0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e};
    static const uint8_t glyph_m[] = {0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11};
    static const uint8_t glyph_n[] = {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11};
    static const uint8_t glyph_o[] = {0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e};
    static const uint8_t glyph_t[] = {0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04};
    static const uint8_t glyph_v[] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x0a, 0x04};
    static const uint8_t glyph_w[] = {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0a};
    static const uint8_t glyph_0[] = {0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e};
    static const uint8_t glyph_1[] = {0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e};
    static const uint8_t glyph_2[] = {0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f};
    static const uint8_t glyph_3[] = {0x1e, 0x01, 0x01, 0x0e, 0x01, 0x01, 0x1e};
    static const uint8_t glyph_4[] = {0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02};
    static const uint8_t glyph_5[] = {0x1f, 0x10, 0x10, 0x1e, 0x01, 0x01, 0x1e};
    static const uint8_t glyph_6[] = {0x0e, 0x10, 0x10, 0x1e, 0x11, 0x11, 0x0e};
    static const uint8_t glyph_7[] = {0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08};
    static const uint8_t glyph_8[] = {0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e};
    static const uint8_t glyph_9[] = {0x0e, 0x11, 0x11, 0x0f, 0x01, 0x01, 0x0e};

    switch (character)
    {
        case 'A': return glyph_a;
        case 'B': return glyph_b;
        case 'C': return glyph_c;
        case 'D': return glyph_d;
        case 'F': return glyph_f;
        case 'G': return glyph_g;
        case 'H': return glyph_h;
        case 'I': return glyph_i;
        case 'K': return glyph_k;
        case 'L': return glyph_l;
        case 'R': return glyph_r;
        case 'E': return glyph_e;
        case 'P': return glyph_p;
        case 'U': return glyph_u;
        case 'S': return glyph_s;
        case 'M': return glyph_m;
        case 'N': return glyph_n;
        case 'O': return glyph_o;
        case 'T': return glyph_t;
        case 'V': return glyph_v;
        case 'W': return glyph_w;
        case '0': return glyph_0;
        case '1': return glyph_1;
        case '2': return glyph_2;
        case '3': return glyph_3;
        case '4': return glyph_4;
        case '5': return glyph_5;
        case '6': return glyph_6;
        case '7': return glyph_7;
        case '8': return glyph_8;
        case '9': return glyph_9;
        default: return NULL;
    }
}

static void draw_text(uint16_t *strip, int strip_y,
                      int x, int y, const char *text, int scale,
                      uint16_t color)
{
    for (; *text; ++text, x += 6 * scale)
    {
        const uint8_t *rows = glyph_rows(*text);
        if (!rows)
        {
            continue;
        }

        for (int glyph_y = 0; glyph_y < 7; ++glyph_y)
        {
            for (int glyph_x = 0; glyph_x < 5; ++glyph_x)
            {
                if (rows[glyph_y] & (1 << (4 - glyph_x)))
                {
                    fill_rectangle(strip, strip_y,
                                   x + glyph_x * scale,
                                   y + glyph_y * scale,
                                   scale, scale, color);
                }
            }
        }
    }
}

static void draw_text_centered(uint16_t *strip, int strip_y,
                               int center_x, int y,
                               const char *label, int scale,
                               uint16_t color)
{
    const int length = strlen(label);
    const int width = length > 0 ? length * 6 * scale - scale : 0;
    draw_text(strip, strip_y, center_x - width / 2, y,
              label, scale, color);
}

static void draw_sound_button(uint16_t *strip, int strip_y, bool muted,
                              uint16_t edge, uint16_t background,
                              uint16_t icon, uint16_t enabled,
                              uint16_t disabled)
{
    const int center_x = PLATFORM_SOUND_CENTER_X;
    const int center_y = PLATFORM_SOUND_CENTER_Y;

    fill_octagon(strip, strip_y, center_x, center_y + 2,
                 PLATFORM_SOUND_RADIUS, 6, rgb565_be(2, 4, 4));
    fill_octagon(strip, strip_y, center_x, center_y,
                 PLATFORM_SOUND_RADIUS, 6, edge);
    fill_octagon(strip, strip_y, center_x, center_y,
                 PLATFORM_SOUND_RADIUS - 3, 5, background);

    // Speaker body and widening cone.
    fill_rectangle(strip, strip_y, center_x - 12, center_y - 5,
                   6, 10, icon);
    for (int column = 0; column < 8; ++column)
    {
        const int height = 4 + column * 2;
        fill_rectangle(strip, strip_y,
                       center_x - 6 + column,
                       center_y - height / 2,
                       1, height, icon);
    }

    if (muted)
    {
        // A thick red slash remains readable on the small utility button.
        for (int offset = -12; offset <= 12; ++offset)
        {
            fill_rectangle(strip, strip_y,
                           center_x + offset - 1,
                           center_y + offset - 1,
                           3, 3, disabled);
        }
    }
    else
    {
        // Three simple sound-wave marks to the right of the cone.
        fill_rectangle(strip, strip_y, center_x + 5, center_y - 3,
                       2, 6, enabled);
        fill_rectangle(strip, strip_y, center_x + 9, center_y - 6,
                       2, 12, enabled);
        fill_rectangle(strip, strip_y, center_x + 13, center_y - 9,
                       2, 18, enabled);
    }
}

static void draw_patch_scaled(uint16_t *strip, int strip_y,
                              int center_x, int center_y,
                              int maximum_width, int maximum_height,
                              const void *patch_data,
                              const uint16_t *palette)
{
    if (!patch_data || !palette)
    {
        return;
    }

    const patch_t *patch = patch_data;
    const int source_width = SHORT(patch->width);
    const int source_height = SHORT(patch->height);
    if (source_width <= 0 || source_height <= 0 ||
        source_width > 512 || source_height > 512)
    {
        return;
    }

    int width = source_width;
    int height = source_height;
    if (width > maximum_width)
    {
        height = height * maximum_width / width;
        width = maximum_width;
    }
    if (height > maximum_height)
    {
        width = width * maximum_height / height;
        height = maximum_height;
    }
    while (width * 2 <= maximum_width && height * 2 <= maximum_height)
    {
        width *= 2;
        height *= 2;
    }
    if (width <= 0 || height <= 0)
    {
        return;
    }

    const int destination_x = center_x - width / 2;
    const int destination_y = center_y - height / 2;
    if (destination_y >= strip_y + LCD_TRANSFER_LINES ||
        destination_y + height <= strip_y)
    {
        return;
    }

    for (int source_x = 0; source_x < source_width; ++source_x)
    {
        const column_t *column =
            (const column_t *)((const uint8_t *)patch +
                               LONG(patch->columnofs[source_x]));
        const int x0 = destination_x + source_x * width / source_width;
        int x1 = destination_x + (source_x + 1) * width / source_width;
        if (x1 <= x0)
        {
            x1 = x0 + 1;
        }

        while (column->topdelta != 0xff)
        {
            const uint8_t *source = (const uint8_t *)column + 3;
            for (int pixel = 0; pixel < column->length; ++pixel)
            {
                const int source_y = column->topdelta + pixel;
                if (source_y >= source_height)
                {
                    break;
                }
                const int y0 = destination_y +
                               source_y * height / source_height;
                int y1 = destination_y +
                         (source_y + 1) * height / source_height;
                if (y1 <= y0)
                {
                    y1 = y0 + 1;
                }
                fill_rectangle(strip, strip_y,
                               x0, y0, x1 - x0, y1 - y0,
                               palette[source[pixel]]);
            }
            column = (const column_t *)((const uint8_t *)column +
                                        column->length + 4);
        }
    }
}

static void draw_close_button(uint16_t *strip, int strip_y)
{
    const int center_x = (PLATFORM_WEAPON_CLOSE_LEFT +
                          PLATFORM_WEAPON_CLOSE_RIGHT) / 2;
    const int center_y = (PLATFORM_WEAPON_CLOSE_TOP +
                          PLATFORM_WEAPON_CLOSE_BOTTOM) / 2;
    const uint16_t shadow = rgb565_be(2, 4, 4);
    const uint16_t edge = rgb565_be(112, 28, 25);
    const uint16_t red = rgb565_be(190, 42, 32);
    const uint16_t white = rgb565_be(246, 231, 200);

    fill_octagon(strip, strip_y, center_x, center_y + 2, 16, 5, shadow);
    fill_octagon(strip, strip_y, center_x, center_y, 16, 5, edge);
    fill_octagon(strip, strip_y, center_x, center_y, 12, 4, red);
    for (int offset = -6; offset <= 6; ++offset)
    {
        fill_rectangle(strip, strip_y,
                       center_x + offset - 1, center_y + offset - 1,
                       3, 3, white);
        fill_rectangle(strip, strip_y,
                       center_x + offset - 1, center_y - offset - 1,
                       3, 3, white);
    }
}

static void render_weapon_selector_strip(uint16_t *strip, int strip_y)
{
    static const char *const labels[PLATFORM_WEAPON_COUNT] = {
        "FIST", "PISTOL", "SHOTGUN",
        "CHAINGUN", "ROCKET", "PLASMA",
        "BFG", "CHAINSAW", "SSG",
    };
    const uint16_t panel = rgb565_be(9, 13, 14);
    const uint16_t panel_highlight = rgb565_be(109, 121, 112);
    const uint16_t tile = rgb565_be(17, 23, 22);
    const uint16_t tile_owned = rgb565_be(25, 34, 29);
    const uint16_t tile_edge = rgb565_be(83, 92, 84);
    const uint16_t tile_disabled_edge = rgb565_be(38, 43, 41);
    const uint16_t selected_edge = rgb565_be(109, 150, 73);
    const uint16_t selected_highlight = rgb565_be(170, 190, 103);
    const uint16_t text = rgb565_be(236, 226, 190);
    const uint16_t disabled_text = rgb565_be(77, 82, 78);
    const uint16_t number = rgb565_be(132, 146, 128);

    memset(strip, 0, LCD_TRANSFER_BYTES);
    fill_rectangle(strip, strip_y, 0, PLATFORM_CONTROLS_TOP,
                   PLATFORM_SCREEN_WIDTH,
                   PLATFORM_SCREEN_HEIGHT - PLATFORM_CONTROLS_TOP,
                   panel);
    fill_rectangle(strip, strip_y, 0, PLATFORM_CONTROLS_TOP,
                   PLATFORM_SCREEN_WIDTH, 3, panel_highlight);
    fill_rectangle(strip, strip_y, 0, PLATFORM_CONTROLS_TOP + 3,
                   PLATFORM_SCREEN_WIDTH, 5, rgb565_be(2, 4, 4));

    draw_text_centered(strip, strip_y, 160, 246,
                       "WEAPONS", 2, text);
    draw_close_button(strip, strip_y);

    for (int weapon = 0; weapon < PLATFORM_WEAPON_COUNT; ++weapon)
    {
        const int column = weapon % PLATFORM_WEAPON_GRID_COLUMNS;
        const int row = weapon / PLATFORM_WEAPON_GRID_COLUMNS;
        const int x = PLATFORM_WEAPON_GRID_LEFT +
                      column * (PLATFORM_WEAPON_CELL_WIDTH +
                                PLATFORM_WEAPON_CELL_GAP_X);
        const int y = PLATFORM_WEAPON_GRID_TOP +
                      row * (PLATFORM_WEAPON_CELL_HEIGHT +
                             PLATFORM_WEAPON_CELL_GAP_Y);
        const uint16_t bit = (uint16_t)(1u << weapon);
        const bool available = (weapon_available_mask & bit) != 0;
        const bool owned = available && (weapon_owned_mask & bit) != 0;
        const bool selected = owned && weapon_selected == weapon;
        const uint16_t edge = selected
                                  ? selected_edge
                                  : (owned ? tile_edge
                                           : tile_disabled_edge);

        fill_chamfered_rectangle(strip, strip_y,
                                 x, y,
                                 PLATFORM_WEAPON_CELL_WIDTH,
                                 PLATFORM_WEAPON_CELL_HEIGHT,
                                 6, edge);
        fill_chamfered_rectangle(strip, strip_y,
                                 x + 2, y + 2,
                                 PLATFORM_WEAPON_CELL_WIDTH - 4,
                                 PLATFORM_WEAPON_CELL_HEIGHT - 4,
                                 5, owned ? tile_owned : tile);
        if (selected)
        {
            fill_rectangle(strip, strip_y,
                           x + 12, y + 3,
                           PLATFORM_WEAPON_CELL_WIDTH - 24, 2,
                           selected_highlight);
        }

        char number_label[2] = {(char)('1' + weapon), '\0'};
        draw_text(strip, strip_y, x + 6, y + 6,
                  number_label, 1, owned ? number : disabled_text);
        draw_patch_scaled(strip, strip_y,
                          x + PLATFORM_WEAPON_CELL_WIDTH / 2,
                          y + 29,
                          76, 30,
                          weapon_patches[weapon],
                          owned ? weapon_color_palette
                                : weapon_gray_palette);
        draw_text_centered(strip, strip_y,
                           x + PLATFORM_WEAPON_CELL_WIDTH / 2,
                           y + 50,
                           labels[weapon], 1,
                           owned ? text : disabled_text);
    }
}

static void render_cheat_selector_strip(uint16_t *strip, int strip_y)
{
    static const char *const labels[PLATFORM_CHEAT_COUNT] = {
        "GOD", "ARSENAL", "AMMO",
        "NOCLIP", "BERSERK", "INVULN",
    };
    const uint16_t panel = rgb565_be(9, 13, 14);
    const uint16_t panel_highlight = rgb565_be(109, 121, 112);
    const uint16_t tile = rgb565_be(22, 27, 25);
    const uint16_t tile_edge = rgb565_be(83, 92, 84);
    const uint16_t active_edge = rgb565_be(109, 150, 73);
    const uint16_t active_highlight = rgb565_be(170, 190, 103);
    const uint16_t text = rgb565_be(236, 226, 190);
    const uint16_t label = rgb565_be(132, 146, 128);

    memset(strip, 0, LCD_TRANSFER_BYTES);
    fill_rectangle(strip, strip_y, 0, PLATFORM_CONTROLS_TOP,
                   PLATFORM_SCREEN_WIDTH,
                   PLATFORM_SCREEN_HEIGHT - PLATFORM_CONTROLS_TOP,
                   panel);
    fill_rectangle(strip, strip_y, 0, PLATFORM_CONTROLS_TOP,
                   PLATFORM_SCREEN_WIDTH, 3, panel_highlight);
    fill_rectangle(strip, strip_y, 0, PLATFORM_CONTROLS_TOP + 3,
                   PLATFORM_SCREEN_WIDTH, 5, rgb565_be(2, 4, 4));

    draw_text_centered(strip, strip_y, 160, 246,
                       "CHEATS", 2, text);
    draw_close_button(strip, strip_y);

    for (int cheat = 0; cheat < PLATFORM_CHEAT_COUNT; ++cheat)
    {
        const int column = cheat % PLATFORM_CHEAT_GRID_COLUMNS;
        const int row = cheat / PLATFORM_CHEAT_GRID_COLUMNS;
        const int x = PLATFORM_CHEAT_GRID_LEFT +
                      column * (PLATFORM_CHEAT_CELL_WIDTH +
                                PLATFORM_CHEAT_CELL_GAP_X);
        const int y = PLATFORM_CHEAT_GRID_TOP +
                      row * (PLATFORM_CHEAT_CELL_HEIGHT +
                             PLATFORM_CHEAT_CELL_GAP_Y);
        const bool active = (cheat_active_mask & (1u << cheat)) != 0;

        fill_chamfered_rectangle(strip, strip_y,
                                 x, y,
                                 PLATFORM_CHEAT_CELL_WIDTH,
                                 PLATFORM_CHEAT_CELL_HEIGHT,
                                 7, active ? active_edge : tile_edge);
        fill_chamfered_rectangle(strip, strip_y,
                                 x + 2, y + 2,
                                 PLATFORM_CHEAT_CELL_WIDTH - 4,
                                 PLATFORM_CHEAT_CELL_HEIGHT - 4,
                                 6, tile);
        if (active)
        {
            fill_rectangle(strip, strip_y,
                           x + 12, y + 3,
                           PLATFORM_CHEAT_CELL_WIDTH - 24, 2,
                           active_highlight);
        }

        draw_patch_scaled(strip, strip_y,
                          x + PLATFORM_CHEAT_CELL_WIDTH / 2,
                          y + 39,
                          70, 48,
                          cheat_patches[cheat],
                          weapon_color_palette);
        draw_text_centered(strip, strip_y,
                           x + PLATFORM_CHEAT_CELL_WIDTH / 2,
                           y + 70,
                           labels[cheat], 1,
                           active ? text : label);
    }
}

static void render_static_strip(uint16_t *strip, int strip_y, bool muted)
{
    const uint16_t panel = rgb565_be(9, 13, 14);
    const uint16_t panel_edge = rgb565_be(48, 58, 56);
    const uint16_t panel_highlight = rgb565_be(109, 121, 112);
    const uint16_t control_edge = rgb565_be(132, 140, 129);
    const uint16_t control_dark = rgb565_be(24, 30, 29);
    const uint16_t control_shadow = rgb565_be(2, 4, 4);
    const uint16_t control_accent = rgb565_be(60, 78, 50);
    const uint16_t control_accent_high = rgb565_be(103, 119, 77);
    const uint16_t fire_edge = rgb565_be(106, 27, 23);
    const uint16_t fire = rgb565_be(174, 38, 29);
    const uint16_t fire_highlight = rgb565_be(231, 83, 55);
    const uint16_t use_edge = rgb565_be(105, 67, 24);
    const uint16_t use = rgb565_be(190, 119, 27);
    const uint16_t use_highlight = rgb565_be(237, 173, 57);
    const uint16_t strafe_blue = rgb565_be(34, 128, 190);
    const uint16_t strafe_white = rgb565_be(242, 245, 235);
    const uint16_t text = rgb565_be(236, 226, 190);
    const uint16_t label = rgb565_be(133, 146, 128);
    const uint16_t sound_enabled = rgb565_be(75, 196, 112);
    const uint16_t sound_disabled = rgb565_be(220, 58, 47);

    memset(strip, 0, LCD_TRANSFER_BYTES);
    fill_rectangle(strip, strip_y, 0, PLATFORM_CONTROLS_TOP,
                   PLATFORM_SCREEN_WIDTH,
                   PLATFORM_SCREEN_HEIGHT - PLATFORM_CONTROLS_TOP,
                   panel);

    // Steel lip between the framebuffer and the physical controls.
    fill_rectangle(strip, strip_y, 0, PLATFORM_CONTROLS_TOP,
                   PLATFORM_SCREEN_WIDTH, 3, panel_highlight);
    fill_rectangle(strip, strip_y, 0, PLATFORM_CONTROLS_TOP + 3,
                   PLATFORM_SCREEN_WIDTH, 5, control_shadow);

    // Two simple recessed bays. A single thin rim is much easier to read on
    // the small LCD than the previous stack of decorative borders.
    fill_chamfered_rectangle(strip, strip_y, 6, 248, 180, 190, 9,
                             panel_edge);
    fill_chamfered_rectangle(strip, strip_y, 8, 250, 176, 186, 7,
                             panel);
    fill_chamfered_rectangle(strip, strip_y, 192, 248, 123, 190, 9,
                             panel_edge);
    fill_chamfered_rectangle(strip, strip_y, 194, 250, 119, 186, 7,
                             panel);

    draw_text(strip, strip_y, 18, 258, "NAV", 1, label);
    draw_text(strip, strip_y, 143, 237, "UAC", 1, label);
    draw_text(strip, strip_y, 230, 239, "ACTION", 1, label);

    // Direction-pad backplate and touch origin share these constants. Moving
    // the artwork therefore cannot silently desynchronise the controls.
    const int pad_x = PLATFORM_JOYSTICK_CENTER_X;
    const int pad_y = PLATFORM_JOYSTICK_CENTER_Y;
    fill_octagon(strip, strip_y,
                 pad_x, pad_y + 3,
                 74, 21, control_shadow);
    fill_octagon(strip, strip_y,
                 pad_x, pad_y,
                 74, 21, panel_edge);
    fill_octagon(strip, strip_y,
                 pad_x, pad_y,
                 70, 19, control_dark);

    const draw_point_t up_outer[] = {
        {pad_x - 18, pad_y - 10}, {pad_x - 18, pad_y - 45},
        {pad_x - 8, pad_y - 62}, {pad_x + 8, pad_y - 62},
        {pad_x + 18, pad_y - 45}, {pad_x + 18, pad_y - 10},
        {pad_x + 10, pad_y - 3}, {pad_x - 10, pad_y - 3},
    };
    const draw_point_t up_inner[] = {
        {pad_x - 13, pad_y - 13}, {pad_x - 13, pad_y - 42},
        {pad_x - 5, pad_y - 55}, {pad_x + 5, pad_y - 55},
        {pad_x + 13, pad_y - 42}, {pad_x + 13, pad_y - 13},
        {pad_x + 7, pad_y - 8}, {pad_x - 7, pad_y - 8},
    };
    const draw_point_t down_outer[] = {
        {pad_x - 10, pad_y + 3}, {pad_x + 10, pad_y + 3},
        {pad_x + 18, pad_y + 10}, {pad_x + 18, pad_y + 45},
        {pad_x + 8, pad_y + 62}, {pad_x - 8, pad_y + 62},
        {pad_x - 18, pad_y + 45}, {pad_x - 18, pad_y + 10},
    };
    const draw_point_t down_inner[] = {
        {pad_x - 7, pad_y + 8}, {pad_x + 7, pad_y + 8},
        {pad_x + 13, pad_y + 13}, {pad_x + 13, pad_y + 42},
        {pad_x + 5, pad_y + 55}, {pad_x - 5, pad_y + 55},
        {pad_x - 13, pad_y + 42}, {pad_x - 13, pad_y + 13},
    };
    const draw_point_t left_outer[] = {
        {pad_x - 10, pad_y - 18}, {pad_x - 3, pad_y - 10},
        {pad_x - 3, pad_y + 10}, {pad_x - 10, pad_y + 18},
        {pad_x - 45, pad_y + 18}, {pad_x - 62, pad_y + 8},
        {pad_x - 62, pad_y - 8}, {pad_x - 45, pad_y - 18},
    };
    const draw_point_t left_inner[] = {
        {pad_x - 13, pad_y - 13}, {pad_x - 8, pad_y - 7},
        {pad_x - 8, pad_y + 7}, {pad_x - 13, pad_y + 13},
        {pad_x - 42, pad_y + 13}, {pad_x - 55, pad_y + 5},
        {pad_x - 55, pad_y - 5}, {pad_x - 42, pad_y - 13},
    };
    const draw_point_t right_outer[] = {
        {pad_x + 10, pad_y - 18}, {pad_x + 45, pad_y - 18},
        {pad_x + 62, pad_y - 8}, {pad_x + 62, pad_y + 8},
        {pad_x + 45, pad_y + 18}, {pad_x + 10, pad_y + 18},
        {pad_x + 3, pad_y + 10}, {pad_x + 3, pad_y - 10},
    };
    const draw_point_t right_inner[] = {
        {pad_x + 13, pad_y - 13}, {pad_x + 42, pad_y - 13},
        {pad_x + 55, pad_y - 5}, {pad_x + 55, pad_y + 5},
        {pad_x + 42, pad_y + 13}, {pad_x + 13, pad_y + 13},
        {pad_x + 8, pad_y + 7}, {pad_x + 8, pad_y - 7},
    };
    fill_convex_polygon(strip, strip_y, up_outer, 8, control_edge);
    fill_convex_polygon(strip, strip_y, up_inner, 8, control_accent);
    fill_convex_polygon(strip, strip_y, down_outer, 8, control_edge);
    fill_convex_polygon(strip, strip_y, down_inner, 8, control_accent);
    fill_convex_polygon(strip, strip_y, left_outer, 8, control_edge);
    fill_convex_polygon(strip, strip_y, left_inner, 8, control_accent);
    fill_convex_polygon(strip, strip_y, right_outer, 8, control_edge);
    fill_convex_polygon(strip, strip_y, right_inner, 8, control_accent);

    // Direction chevrons.
    const draw_point_t arrow_up[] = {
        {pad_x, pad_y - 49}, {pad_x - 7, pad_y - 38},
        {pad_x + 7, pad_y - 38},
    };
    const draw_point_t arrow_down[] = {
        {pad_x - 7, pad_y + 38}, {pad_x + 7, pad_y + 38},
        {pad_x, pad_y + 49},
    };
    const draw_point_t arrow_left[] = {
        {pad_x - 49, pad_y}, {pad_x - 38, pad_y - 7},
        {pad_x - 38, pad_y + 7},
    };
    const draw_point_t arrow_right[] = {
        {pad_x + 38, pad_y - 7}, {pad_x + 49, pad_y},
        {pad_x + 38, pad_y + 7},
    };
    fill_convex_polygon(strip, strip_y, arrow_up, 3, control_accent_high);
    fill_convex_polygon(strip, strip_y, arrow_down, 3, control_accent_high);
    fill_convex_polygon(strip, strip_y, arrow_left, 3, control_accent_high);
    fill_convex_polygon(strip, strip_y, arrow_right, 3, control_accent_high);

    fill_octagon(strip, strip_y,
                 pad_x, pad_y + 2,
                 21, 6, control_shadow);
    fill_octagon(strip, strip_y,
                 pad_x, pad_y,
                 21, 6, control_edge);
    fill_octagon(strip, strip_y,
                 pad_x, pad_y,
                 17, 5, control_dark);
    fill_rectangle(strip, strip_y, pad_x - 6, pad_y - 5,
                   2, 10, control_accent_high);
    fill_rectangle(strip, strip_y, pad_x - 1, pad_y - 7,
                   2, 12, control_accent_high);
    fill_rectangle(strip, strip_y, pad_x + 4, pad_y - 3,
                   2, 8, control_accent_high);

    // Main action key, visually smaller than the legacy circle while keeping
    // its generous right-hand touch region.
    fill_octagon(strip, strip_y, 250, 302, 38, 11, control_shadow);
    fill_octagon(strip, strip_y, 250, 299, 38, 11, fire_edge);
    fill_octagon(strip, strip_y, 250, 299, 33, 9, fire);
    fill_rectangle(strip, strip_y, 234, 272, 32, 2, fire_highlight);
    draw_text(strip, strip_y, 227, 292, "FIRE", 2, text);

    fill_octagon(strip, strip_y, 226, 400, 29, 8, control_shadow);
    fill_octagon(strip, strip_y, 226, 397, 29, 8, use_edge);
    fill_octagon(strip, strip_y, 226, 397, 24, 7, use);
    fill_rectangle(strip, strip_y, 214, 378, 24, 2, use_highlight);
    draw_text_centered(strip, strip_y, 226, 390, "USE", 2, text);

    // Compact latching strafe key. Its colours invert so the current mode is
    // readable at a glance without consuming the two available touch points.
    fill_octagon(strip, strip_y, 287, 400, 21, 7, control_shadow);
    fill_octagon(strip, strip_y, 287, 397, 21, 7,
                 strafe_mode ? strafe_white : strafe_blue);
    fill_octagon(strip, strip_y, 287, 397, 17, 5,
                 strafe_mode ? strafe_blue : strafe_white);
    draw_text_centered(strip, strip_y, 287, 394, "STF", 1,
                       strafe_mode ? strafe_white : strafe_blue);

    // Shared utility rail: audio, modal panels, and DOOM's own menu.
    fill_chamfered_rectangle(strip, strip_y, 7, 443, 306, 34, 7,
                             panel_edge);
    fill_chamfered_rectangle(strip, strip_y, 10, 446, 300, 28, 5,
                             control_dark);

    fill_chamfered_rectangle(strip, strip_y, 51, 447, 75, 26, 6,
                             weapon_selector_enabled
                                 ? control_edge
                                 : panel_edge);
    fill_chamfered_rectangle(strip, strip_y, 55, 451, 67, 18, 4,
                             panel);
    draw_text(strip, strip_y, 64, 453, "WEAP", 2,
              weapon_selector_enabled ? text : label);

    fill_chamfered_rectangle(strip, strip_y, 130, 447, 78, 26, 6,
                             weapon_selector_enabled
                                 ? control_edge
                                 : panel_edge);
    fill_chamfered_rectangle(strip, strip_y, 134, 451, 70, 18, 4,
                             panel);
    draw_text(strip, strip_y, 139, 453, "CHEAT", 2,
              weapon_selector_enabled ? text : label);

    fill_chamfered_rectangle(strip, strip_y, 211, 447, 92, 26, 6,
                             control_edge);
    fill_chamfered_rectangle(strip, strip_y, 215, 451, 84, 18, 4,
                             panel);
    draw_text(strip, strip_y, 234, 453, "MENU", 2, text);

    draw_sound_button(strip, strip_y, muted,
                      control_edge, control_dark, text,
                      sound_enabled, sound_disabled);
}

static void refresh_controls_frame(bool muted)
{
    const platform_ui_mode_t mode = ui_mode;

    for (int y = PLATFORM_LOWER_TOP;
         y < LCD_NATIVE_V_RES;
         y += LCD_TRANSFER_LINES)
    {
        if (mode == PLATFORM_UI_WEAPONS)
        {
            render_weapon_selector_strip(transfer_buffers[0], y);
        }
        else if (mode == PLATFORM_UI_CHEATS)
        {
            render_cheat_selector_strip(transfer_buffers[0], y);
        }
        else
        {
            render_static_strip(transfer_buffers[0], y, muted);
        }
        memcpy(controls_frame +
                   (y - PLATFORM_LOWER_TOP) * LCD_NATIVE_H_RES,
               transfer_buffers[0], LCD_TRANSFER_BYTES);
    }
}

static void compose_controls_strip(uint16_t *strip, int strip_y,
                                   const uint16_t *statusbar,
                                   int statusbar_height)
{
    memcpy(strip,
           controls_frame +
               (strip_y - PLATFORM_LOWER_TOP) * LCD_NATIVE_H_RES,
           LCD_TRANSFER_BYTES);

    if (!statusbar || statusbar_height <= 0)
    {
        return;
    }

    const int statusbar_y = PLATFORM_STATUSBAR_TOP;
    const int statusbar_end = statusbar_y + statusbar_height;
    const int copy_y0 = strip_y > statusbar_y ? strip_y : statusbar_y;
    const int copy_y1 = strip_y + LCD_TRANSFER_LINES < statusbar_end
                            ? strip_y + LCD_TRANSFER_LINES
                            : statusbar_end;

    if (copy_y0 >= copy_y1)
    {
        return;
    }

    for (int y = copy_y0; y < copy_y1; ++y)
    {
        memcpy(strip + (y - strip_y) * LCD_NATIVE_H_RES,
               statusbar + (y - statusbar_y) * LCD_NATIVE_H_RES,
               LCD_NATIVE_H_RES * sizeof(uint16_t));
    }
}

static void redraw_controls_if_needed(const uint16_t *statusbar,
                                      int statusbar_height,
                                      bool statusbar_dirty)
{
    const bool sound_dirty = sound_button_dirty;
    const bool ui_dirty = panel_dirty;

    if ((!sound_dirty && !ui_dirty && !statusbar_dirty) || !lcd_panel)
    {
        return;
    }

    // Clear first so a concurrent second toggle remains pending for the next
    // frame. All LCD writes stay on the render task, away from the touch task.
    if (sound_dirty || ui_dirty)
    {
        sound_button_dirty = false;
        panel_dirty = false;
        refresh_controls_frame(sound_muted);
    }

    // The AXS15231B QSPI driver does not send a vertical address window for
    // continuation writes: every strip after y=0 is appended to the preceding
    // one. platform_lcd_present_rgb565() has just written lines 0..199, so
    // redraw the complete lower area sequentially from line 200. Starting
    // directly at the changed region would instead duplicate those strips at
    // the top of the lower area.
    for (int y = PLATFORM_LOWER_TOP, buffer_index = 0;
         y < LCD_NATIVE_V_RES;
         y += LCD_TRANSFER_LINES, buffer_index ^= 1)
    {
        compose_controls_strip(transfer_buffers[buffer_index], y,
                               statusbar, statusbar_height);
        draw_native_strip(y, transfer_buffers[buffer_index]);
    }
}

void platform_lcd_present_rgb565(const uint16_t *framebuffer,
                                 int width,
                                 int height,
                                 int stride,
                                 const uint16_t *statusbar,
                                 int statusbar_height,
                                 bool statusbar_dirty)
{
    if (!lcd_panel || !framebuffer || !transfer_buffers[0] ||
        !transfer_buffers[1] || !controls_frame)
    {
        return;
    }

    if (width != LCD_GAME_WIDTH || height != LCD_GAME_HEIGHT || stride < width)
    {
        ESP_LOGE(TAG, "Unexpected framebuffer layout %dx%d (stride %d)",
                 width, height, stride);
        return;
    }

    if (statusbar && statusbar_height != PLATFORM_STATUSBAR_HEIGHT)
    {
        ESP_LOGE(TAG, "Unexpected status-bar height %d", statusbar_height);
        statusbar = NULL;
        statusbar_height = 0;
        statusbar_dirty = true;
    }

    for (int y = 0, buffer_index = 0; y < LCD_GAME_HEIGHT;
         y += LCD_TRANSFER_LINES, buffer_index ^= 1)
    {
        uint16_t *transfer_buffer = transfer_buffers[buffer_index];
        for (int line = 0; line < LCD_TRANSFER_LINES; ++line)
        {
            memcpy(transfer_buffer + line * LCD_NATIVE_H_RES,
                   framebuffer + (y + line) * stride,
                   LCD_GAME_WIDTH * sizeof(uint16_t));
        }
        draw_native_strip(y, transfer_buffer);
    }

    redraw_controls_if_needed(statusbar, statusbar_height, statusbar_dirty);
}

void platform_lcd_set_sound_muted(bool muted)
{
    sound_muted = muted;
    sound_button_dirty = true;
}

void platform_lcd_set_strafe_mode(bool enabled)
{
    if (strafe_mode != enabled)
    {
        strafe_mode = enabled;
        panel_dirty = true;
    }
}

void platform_lcd_set_ui_mode(platform_ui_mode_t mode)
{
    if (mode != PLATFORM_UI_NORMAL &&
        mode != PLATFORM_UI_WEAPONS &&
        mode != PLATFORM_UI_CHEATS)
    {
        mode = PLATFORM_UI_NORMAL;
    }
    if (ui_mode != mode)
    {
        ui_mode = mode;
        panel_dirty = true;
    }
}

platform_ui_mode_t platform_lcd_get_ui_mode(void)
{
    return ui_mode;
}

void platform_lcd_set_weapon_assets(const void *const *patches,
                                    uint16_t available_mask,
                                    const uint16_t *color_palette,
                                    const uint16_t *gray_palette)
{
    for (int weapon = 0; weapon < PLATFORM_WEAPON_COUNT; ++weapon)
    {
        weapon_patches[weapon] = patches ? patches[weapon] : NULL;
    }
    weapon_available_mask = available_mask;
    weapon_color_palette = color_palette;
    weapon_gray_palette = gray_palette;
    panel_dirty = true;
}

void platform_lcd_set_weapon_state(uint16_t owned_mask,
                                   int selected_weapon,
                                   bool selector_enabled)
{
    if (weapon_owned_mask != owned_mask ||
        weapon_selected != selected_weapon ||
        weapon_selector_enabled != selector_enabled)
    {
        weapon_owned_mask = owned_mask;
        weapon_selected = selected_weapon;
        weapon_selector_enabled = selector_enabled;
        panel_dirty = true;
    }
}

void platform_lcd_set_cheat_assets(const void *const *patches)
{
    for (int cheat = 0; cheat < PLATFORM_CHEAT_COUNT; ++cheat)
    {
        cheat_patches[cheat] = patches ? patches[cheat] : NULL;
    }
    panel_dirty = true;
}

void platform_lcd_set_cheat_state(uint16_t active_mask)
{
    if (cheat_active_mask != active_mask)
    {
        cheat_active_mask = active_mask;
        panel_dirty = true;
    }
}

void platform_lcd_invalidate_weapon_palette(void)
{
    if (ui_mode == PLATFORM_UI_WEAPONS ||
        ui_mode == PLATFORM_UI_CHEATS)
    {
        panel_dirty = true;
    }
}

void platform_lcd_init(void)
{
    const spi_bus_config_t bus_config =
        AXS15231B_PANEL_BUS_QSPI_CONFIG(LCD_GPIO_CLK,
                                       LCD_GPIO_D0,
                                       LCD_GPIO_D1,
                                       LCD_GPIO_D2,
                                       LCD_GPIO_D3,
                                       LCD_TRANSFER_BYTES);
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_SPI_HOST, &bus_config, SPI_DMA_CH_AUTO));

    const esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = LCD_GPIO_CS,
        .dc_gpio_num = GPIO_NUM_NC,
        .spi_mode = 3,
        .pclk_hz = LCD_PCLK_HZ,
        .trans_queue_depth = 1,
        .lcd_cmd_bits = 32,
        .lcd_param_bits = 8,
        .flags = {
            .quad_mode = true,
        },
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI_HOST,
                                             &io_config, &lcd_io));

    const axs15231b_vendor_config_t vendor_config = {
        .init_cmds = jc3248w535_init_commands,
        .init_cmds_size = sizeof(jc3248w535_init_commands) /
                          sizeof(jc3248w535_init_commands[0]),
        .flags = {
            .use_qspi_interface = 1,
        },
    };
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_GPIO_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .data_endian = LCD_RGB_DATA_ENDIAN_BIG,
        .bits_per_pixel = 16,
        .vendor_config = (void *)&vendor_config,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_axs15231b(lcd_io, &panel_config, &lcd_panel));

    for (int i = 0; i < 2; ++i)
    {
        transfer_buffers[i] = heap_caps_aligned_alloc(64, LCD_TRANSFER_BYTES,
                                                       MALLOC_CAP_DMA |
                                                       MALLOC_CAP_INTERNAL |
                                                       MALLOC_CAP_8BIT);
        if (!transfer_buffers[i])
        {
            ESP_LOGE(TAG, "Unable to allocate LCD DMA buffer %d", i);
            abort();
        }
    }

    controls_frame = heap_caps_malloc(LCD_CONTROLS_BYTES,
                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!controls_frame)
    {
        ESP_LOGE(TAG, "Unable to allocate cached controls framebuffer");
        abort();
    }

    const gpio_config_t backlight_config = {
        .pin_bit_mask = 1ULL << LCD_GPIO_BL,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&backlight_config));
    ESP_ERROR_CHECK(gpio_set_level(LCD_GPIO_BL, 0));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(lcd_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(lcd_panel));
    // Keep the controller in its native 320x480 portrait orientation.  DOOM's
    // framebuffer then maps directly to the first 320x200 pixels.
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(lcd_panel, false));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(lcd_panel, false, false));

    refresh_controls_frame(false);

    for (int y = 0, buffer_index = 0; y < LCD_NATIVE_V_RES;
         y += LCD_TRANSFER_LINES, buffer_index ^= 1)
    {
        if (y < PLATFORM_LOWER_TOP)
        {
            memset(transfer_buffers[buffer_index], 0, LCD_TRANSFER_BYTES);
        }
        else
        {
            compose_controls_strip(transfer_buffers[buffer_index], y,
                                   NULL, 0);
        }
        draw_native_strip(y, transfer_buffers[buffer_index]);
    }

    // esp_lcd_axs15231b 2.1.0 still implements the legacy "disp_off"
    // callback semantics: false sends DISPON and true sends DISPOFF.  The
    // current esp_lcd API names this argument "on_off", so compensate here.
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(lcd_panel, false));

    ESP_ERROR_CHECK(gpio_set_level(LCD_GPIO_BL, 1));
    ESP_LOGI(TAG, "JC3248W535 LCD ready (%dx%d portrait, game %dx%d)",
             LCD_NATIVE_H_RES, LCD_NATIVE_V_RES,
             LCD_GAME_WIDTH, LCD_GAME_HEIGHT);
}
