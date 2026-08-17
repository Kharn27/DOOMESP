#include "platform_input.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_lcd_axs15231b.h"
#include "esp_lcd_panel_io.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "doomdef.h"
#include "platform_audio.h"
#include "platform_controls.h"
#include "platform_lcd.h"

#define TOUCH_I2C_PORT I2C_NUM_0
#define TOUCH_GPIO_SDA 4
#define TOUCH_GPIO_SCL 8
#define TOUCH_GPIO_INT 3
#define TOUCH_I2C_HZ 400000
#define TOUCH_EVENT_QUEUE_LENGTH 16
#define PAD_REPEAT_DELAY_MS 350
#define PAD_REPEAT_PERIOD_MS 120

typedef enum
{
    TOUCH_CONTROL_NONE,
    TOUCH_CONTROL_PAD,
    TOUCH_CONTROL_FIRE,
    TOUCH_CONTROL_USE,
    TOUCH_CONTROL_MENU,
    TOUCH_CONTROL_SOUND,
} touch_control_t;

typedef struct
{
    int buttons;
    int x;
    int y;
    bool escape;
} touch_control_state_t;

static const char *TAG = "platform_input";
static i2c_master_bus_handle_t touch_i2c_bus;
static esp_lcd_panel_io_handle_t touch_io;
static QueueHandle_t touch_event_queue;
static touch_control_state_t previous_state;
static touch_control_t active_control;
static bool touch_contact_active;
static bool event_delivered_this_tic;
static bool touch_read_error_logged;
static bool touch_queue_full_logged;
static TaskHandle_t touch_task_handle;
static TickType_t pad_repeat_deadline;

static const char *control_name(touch_control_t control)
{
    switch (control)
    {
        case TOUCH_CONTROL_PAD: return "PAD";
        case TOUCH_CONTROL_FIRE: return "FIRE";
        case TOUCH_CONTROL_USE: return "USE";
        case TOUCH_CONTROL_MENU: return "MENU";
        case TOUCH_CONTROL_SOUND: return "SOUND";
        default: return "NONE";
    }
}

static touch_control_t control_at(uint16_t x, uint16_t y)
{
    if (x >= PLATFORM_SCREEN_WIDTH || y >= PLATFORM_SCREEN_HEIGHT ||
        y < PLATFORM_CONTROLS_TOP)
    {
        return TOUCH_CONTROL_NONE;
    }
    const int sound_dx = (int)x - PLATFORM_SOUND_CENTER_X;
    const int sound_dy = (int)y - PLATFORM_SOUND_CENTER_Y;
    if (sound_dx * sound_dx + sound_dy * sound_dy <=
        PLATFORM_SOUND_TOUCH_RADIUS * PLATFORM_SOUND_TOUCH_RADIUS)
    {
        return TOUCH_CONTROL_SOUND;
    }
    if (x < PLATFORM_JOYSTICK_RIGHT)
    {
        return TOUCH_CONTROL_PAD;
    }
    if (y < PLATFORM_FIRE_BOTTOM)
    {
        return TOUCH_CONTROL_FIRE;
    }
    if (y < PLATFORM_USE_BOTTOM)
    {
        return TOUCH_CONTROL_USE;
    }
    return TOUCH_CONTROL_MENU;
}

static touch_control_state_t controls_from_touch(uint16_t x, uint16_t y,
                                                  touch_control_t control)
{
    touch_control_state_t state = {0};

    if (control == TOUCH_CONTROL_PAD)
    {
        const int dx = (int)x - PLATFORM_JOYSTICK_CENTER_X;
        const int dy = (int)y - PLATFORM_JOYSTICK_CENTER_Y;

        if (abs(dx) > PLATFORM_JOYSTICK_DEAD_ZONE)
        {
            state.x = dx < 0 ? -1 : 1;
        }
        if (abs(dy) > PLATFORM_JOYSTICK_DEAD_ZONE)
        {
            state.y = dy < 0 ? -1 : 1;
        }
    }
    else if (control == TOUCH_CONTROL_FIRE)
    {
        // Joystick button 0: fire in game, validate in menus.
        state.buttons = 1;
    }
    else if (control == TOUCH_CONTROL_USE)
    {
        // Joystick button 3: activate doors and switches.
        state.buttons = 8;
    }
    else if (control == TOUCH_CONTROL_MENU)
    {
        state.escape = true;
    }

    return state;
}

static void send_event(event_t event)
{
    if (xQueueSend(touch_event_queue, &event, 0) != pdTRUE &&
        !touch_queue_full_logged)
    {
        ESP_LOGW(TAG, "Touch event queue full; input changes are too fast");
        touch_queue_full_logged = true;
    }
}

static void queue_state_changes(touch_control_state_t state)
{
    if (state.escape != previous_state.escape)
    {
        send_event((event_t){
            .type = state.escape ? ev_keydown : ev_keyup,
            .data1 = KEY_ESCAPE,
        });
    }

    if (state.buttons != previous_state.buttons ||
        state.x != previous_state.x || state.y != previous_state.y)
    {
        send_event((event_t){
            .type = ev_joystick,
            .data1 = state.buttons,
            .data2 = state.x,
            .data3 = state.y,
        });
    }

    previous_state = state;
}

static esp_err_t read_touch_packet(uint8_t data[8])
{
    static const uint8_t read_command[11] = {
        0xb5, 0xab, 0xa5, 0x5a,
        0x00, 0x00, 0x00, 0x08,
        0x00, 0x00, 0x00,
    };

    esp_err_t err = esp_lcd_panel_io_tx_param(touch_io, -1,
                                               read_command,
                                               sizeof(read_command));
    if (err == ESP_OK)
    {
        err = esp_lcd_panel_io_rx_param(touch_io, -1, data, 8);
    }
    return err;
}

static void release_active_control(void)
{
    if (touch_contact_active)
    {
        queue_state_changes((touch_control_state_t){0});
    }
    touch_contact_active = false;
    active_control = TOUCH_CONTROL_NONE;
    pad_repeat_deadline = 0;
}

static void process_touch_sample(const uint8_t data[8])
{
    const uint8_t gesture = data[0];
    const uint8_t point_count = data[1];
    const uint8_t event_code = data[2] >> 6;

    // In the AXS15231B record, event 1 is lift-up.  A zero point count is the
    // other release form.  Empty packets between scans are no longer observed
    // because this function is called only after the controller's IRQ edge.
    if (point_count == 0 || event_code == 1)
    {
        release_active_control();
        return;
    }

    const uint16_t x = (uint16_t)(((data[2] & 0x0f) << 8) | data[3]);
    const uint16_t y = (uint16_t)(((data[4] & 0x0f) << 8) | data[5]);

    if (!touch_contact_active)
    {
        active_control = control_at(x, y);
        touch_contact_active = true;
        ESP_LOGI(TAG, "Touch down raw=(%u,%u), gesture=%u, event=%u, points=%u -> %s",
                 (unsigned)x, (unsigned)y, (unsigned)gesture,
                 (unsigned)event_code,
                 (unsigned)point_count,
                 control_name(active_control));

        if (active_control == TOUCH_CONTROL_SOUND)
        {
            const bool muted = platform_audio_toggle_mute();
            platform_lcd_set_sound_muted(muted);
        }
    }

    // Keep the control selected at touch-down until release.  Small finger
    // movements can alter a pad direction, but cannot become another button.
    // If the controller suddenly announces extra points, retain the last pad
    // direction; the eight-byte packet cannot identify which finger is which.
    if (point_count > 1 && active_control == TOUCH_CONTROL_PAD)
    {
        return;
    }
    const touch_control_state_t state =
        controls_from_touch(x, y, active_control);
    const bool pad_direction_changed =
        active_control == TOUCH_CONTROL_PAD &&
        (state.x != previous_state.x || state.y != previous_state.y);

    queue_state_changes(state);
    if (pad_direction_changed && (state.x != 0 || state.y != 0))
    {
        pad_repeat_deadline = xTaskGetTickCount() +
                              pdMS_TO_TICKS(PAD_REPEAT_DELAY_MS);
    }
}

static void IRAM_ATTR touch_interrupt_handler(void *argument)
{
    (void)argument;
    BaseType_t higher_priority_task_woken = pdFALSE;
    vTaskNotifyGiveFromISR(touch_task_handle, &higher_priority_task_woken);
    if (higher_priority_task_woken == pdTRUE)
    {
        portYIELD_FROM_ISR();
    }
}

static void touch_event_task(void *argument)
{
    (void)argument;
    uint8_t data[8];

    while (true)
    {
        TickType_t wait_ticks = portMAX_DELAY;
        if (touch_contact_active && active_control == TOUCH_CONTROL_PAD &&
            (previous_state.x != 0 || previous_state.y != 0))
        {
            const TickType_t now = xTaskGetTickCount();
            if ((int32_t)(pad_repeat_deadline - now) <= 0)
            {
                wait_ticks = 0;
            }
            else
            {
                wait_ticks = pad_repeat_deadline - now;
            }
        }

        // Reading outside an IRQ produces intermittent empty packets on this
        // panel.  Wait for its active-low event edge before every transaction.
        const uint32_t interrupt_count =
            ulTaskNotifyTake(pdTRUE, wait_ticks);

        if (interrupt_count == 0)
        {
            // Menus need key-repeat while a direction stays held.  Re-sending
            // the same joystick state is harmless in-game, while FIRE, USE,
            // and MENU deliberately never repeat.
            if (touch_contact_active && active_control == TOUCH_CONTROL_PAD &&
                (previous_state.x != 0 || previous_state.y != 0))
            {
                send_event((event_t){
                    .type = ev_joystick,
                    .data1 = previous_state.buttons,
                    .data2 = previous_state.x,
                    .data3 = previous_state.y,
                });
                pad_repeat_deadline = xTaskGetTickCount() +
                                      pdMS_TO_TICKS(PAD_REPEAT_PERIOD_MS);
            }
            continue;
        }

        memset(data, 0, sizeof(data));
        const esp_err_t err = read_touch_packet(data);
        if (err == ESP_OK)
        {
            touch_read_error_logged = false;
            process_touch_sample(data);
        }
        else if (!touch_read_error_logged)
        {
            ESP_LOGE(TAG, "Touch read failed: %s", esp_err_to_name(err));
            touch_read_error_logged = true;
        }
    }
}

int platform_input_read(event_t *event, ticcmd_t *ticcmd)
{
    (void)ticcmd;

    // D_ProcessEvents calls us repeatedly until we return zero.  Deliver one
    // state change per game tic so a short FIRE press and its release cannot
    // cancel each other before G_BuildTiccmd observes the press.
    if (event_delivered_this_tic)
    {
        event_delivered_this_tic = false;
        return 0;
    }

    if (xQueueReceive(touch_event_queue, event, 0) == pdTRUE)
    {
        event_delivered_this_tic = true;
        return 1;
    }

    return 0;
}

void platform_input_init(void)
{
    const i2c_master_bus_config_t bus_config = {
        .i2c_port = TOUCH_I2C_PORT,
        .sda_io_num = TOUCH_GPIO_SDA,
        .scl_io_num = TOUCH_GPIO_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags = {
            .enable_internal_pullup = true,
        },
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &touch_i2c_bus));

    esp_lcd_panel_io_i2c_config_t io_config =
        ESP_LCD_TOUCH_IO_I2C_AXS15231B_CONFIG();
    io_config.scl_speed_hz = TOUCH_I2C_HZ;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(touch_i2c_bus, &io_config, &touch_io));

    memset(&previous_state, 0, sizeof(previous_state));
    touch_event_queue = xQueueCreate(TOUCH_EVENT_QUEUE_LENGTH, sizeof(event_t));
    if (!touch_event_queue)
    {
        ESP_LOGE(TAG, "Unable to allocate touch event queue");
        abort();
    }

    if (xTaskCreate(touch_event_task, "touch_event", 3072, NULL, 5,
                    &touch_task_handle) != pdPASS)
    {
        ESP_LOGE(TAG, "Unable to create touch event task");
        abort();
    }

    const gpio_config_t interrupt_config = {
        .pin_bit_mask = 1ULL << TOUCH_GPIO_INT,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    ESP_ERROR_CHECK(gpio_config(&interrupt_config));

    esp_err_t err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
    {
        ESP_ERROR_CHECK(err);
    }
    ESP_ERROR_CHECK(gpio_isr_handler_add(TOUCH_GPIO_INT,
                                         touch_interrupt_handler, NULL));

    ESP_LOGI(TAG, "AXS15231B touch ready (%dx%d portrait, interrupt driven)",
             PLATFORM_SCREEN_WIDTH, PLATFORM_SCREEN_HEIGHT);
}
