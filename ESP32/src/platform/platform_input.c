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
#include "platform_board.h"
#include "platform_controls.h"
#include "platform_lcd.h"

#define TOUCH_MAX_POINTS 2
#define TOUCH_ID_COUNT 16
#define TOUCH_POINT_RECORD_BYTES 6
#define TOUCH_PACKET_BYTES (2 + TOUCH_MAX_POINTS * TOUCH_POINT_RECORD_BYTES)
#define TOUCH_EVENT_QUEUE_LENGTH 32
#define PAD_REPEAT_DELAY_MS 350
#define PAD_REPEAT_PERIOD_MS 120
#define JOYSTICK_BUTTON_STRAFE 2

typedef enum
{
    TOUCH_CONTROL_NONE,
    TOUCH_CONTROL_PAD,
    TOUCH_CONTROL_FIRE,
    TOUCH_CONTROL_USE,
    TOUCH_CONTROL_STRAFE,
    TOUCH_CONTROL_MENU,
    TOUCH_CONTROL_SOUND,
    TOUCH_CONTROL_WEAPONS,
    TOUCH_CONTROL_CHEATS,
    TOUCH_CONTROL_WEAPON_CLOSE,
    TOUCH_CONTROL_WEAPON_0,
    TOUCH_CONTROL_WEAPON_1,
    TOUCH_CONTROL_WEAPON_2,
    TOUCH_CONTROL_WEAPON_3,
    TOUCH_CONTROL_WEAPON_4,
    TOUCH_CONTROL_WEAPON_5,
    TOUCH_CONTROL_WEAPON_6,
    TOUCH_CONTROL_WEAPON_7,
    TOUCH_CONTROL_WEAPON_8,
    TOUCH_CONTROL_CHEAT_CLOSE,
    TOUCH_CONTROL_CHEAT_0,
    TOUCH_CONTROL_CHEAT_1,
    TOUCH_CONTROL_CHEAT_2,
    TOUCH_CONTROL_CHEAT_3,
    TOUCH_CONTROL_CHEAT_4,
    TOUCH_CONTROL_CHEAT_5,
} touch_control_t;

typedef struct
{
    int buttons;
    int x;
    int y;
    bool escape;
} touch_control_state_t;

typedef struct
{
    bool active;
    touch_control_t control;
    uint16_t x;
    uint16_t y;
} touch_contact_t;

static const char *TAG = "platform_input";
static i2c_master_bus_handle_t touch_i2c_bus;
static esp_lcd_panel_io_handle_t touch_io;
static QueueHandle_t touch_event_queue;
static QueueHandle_t weapon_request_queue;
static touch_control_state_t previous_state;
static touch_contact_t touch_contacts[TOUCH_ID_COUNT];
static bool event_delivered_this_tic;
static bool touch_read_error_logged;
static bool touch_queue_full_logged;
static TaskHandle_t touch_task_handle;
static TickType_t pad_repeat_deadline;
static volatile uint16_t owned_weapon_mask;
static volatile uint16_t available_weapon_mask;
static volatile bool weapon_selector_enabled;
static bool suppress_until_all_contacts_up;
static bool strafe_mode;

static const char *control_name(touch_control_t control)
{
    switch (control)
    {
        case TOUCH_CONTROL_PAD: return "PAD";
        case TOUCH_CONTROL_FIRE: return "FIRE";
        case TOUCH_CONTROL_USE: return "USE";
        case TOUCH_CONTROL_STRAFE: return "STRAFE";
        case TOUCH_CONTROL_MENU: return "MENU";
        case TOUCH_CONTROL_SOUND: return "SOUND";
        case TOUCH_CONTROL_WEAPONS: return "WEAPONS";
        case TOUCH_CONTROL_CHEATS: return "CHEATS";
        case TOUCH_CONTROL_WEAPON_CLOSE: return "WEAPON_CLOSE";
        case TOUCH_CONTROL_WEAPON_0:
        case TOUCH_CONTROL_WEAPON_1:
        case TOUCH_CONTROL_WEAPON_2:
        case TOUCH_CONTROL_WEAPON_3:
        case TOUCH_CONTROL_WEAPON_4:
        case TOUCH_CONTROL_WEAPON_5:
        case TOUCH_CONTROL_WEAPON_6:
        case TOUCH_CONTROL_WEAPON_7:
        case TOUCH_CONTROL_WEAPON_8:
            return "WEAPON_SLOT";
        case TOUCH_CONTROL_CHEAT_CLOSE: return "CHEAT_CLOSE";
        case TOUCH_CONTROL_CHEAT_0:
        case TOUCH_CONTROL_CHEAT_1:
        case TOUCH_CONTROL_CHEAT_2:
        case TOUCH_CONTROL_CHEAT_3:
        case TOUCH_CONTROL_CHEAT_4:
        case TOUCH_CONTROL_CHEAT_5:
            return "CHEAT_SLOT";
        default: return "NONE";
    }
}

static int weapon_index_for_control(touch_control_t control)
{
    if (control < TOUCH_CONTROL_WEAPON_0 ||
        control > TOUCH_CONTROL_WEAPON_8)
    {
        return -1;
    }
    return control - TOUCH_CONTROL_WEAPON_0;
}

static touch_control_t weapon_selector_control_at(uint16_t x, uint16_t y)
{
    if (x >= PLATFORM_WEAPON_CLOSE_LEFT &&
        x < PLATFORM_WEAPON_CLOSE_RIGHT &&
        y >= PLATFORM_WEAPON_CLOSE_TOP &&
        y < PLATFORM_WEAPON_CLOSE_BOTTOM)
    {
        return TOUCH_CONTROL_WEAPON_CLOSE;
    }

    if (x < PLATFORM_WEAPON_GRID_LEFT || y < PLATFORM_WEAPON_GRID_TOP)
    {
        return TOUCH_CONTROL_NONE;
    }

    const int cell_step_x = PLATFORM_WEAPON_CELL_WIDTH +
                            PLATFORM_WEAPON_CELL_GAP_X;
    const int cell_step_y = PLATFORM_WEAPON_CELL_HEIGHT +
                            PLATFORM_WEAPON_CELL_GAP_Y;
    const int relative_x = x - PLATFORM_WEAPON_GRID_LEFT;
    const int relative_y = y - PLATFORM_WEAPON_GRID_TOP;
    const int column = relative_x / cell_step_x;
    const int row = relative_y / cell_step_y;

    if (column >= PLATFORM_WEAPON_GRID_COLUMNS || row >= 3 ||
        relative_x % cell_step_x >= PLATFORM_WEAPON_CELL_WIDTH ||
        relative_y % cell_step_y >= PLATFORM_WEAPON_CELL_HEIGHT)
    {
        return TOUCH_CONTROL_NONE;
    }

    const int weapon = row * PLATFORM_WEAPON_GRID_COLUMNS + column;
    return (touch_control_t)(TOUCH_CONTROL_WEAPON_0 + weapon);
}

static int cheat_index_for_control(touch_control_t control)
{
    if (control < TOUCH_CONTROL_CHEAT_0 ||
        control > TOUCH_CONTROL_CHEAT_5)
    {
        return -1;
    }
    return control - TOUCH_CONTROL_CHEAT_0;
}

static touch_control_t cheat_selector_control_at(uint16_t x, uint16_t y)
{
    if (x >= PLATFORM_WEAPON_CLOSE_LEFT &&
        x < PLATFORM_WEAPON_CLOSE_RIGHT &&
        y >= PLATFORM_WEAPON_CLOSE_TOP &&
        y < PLATFORM_WEAPON_CLOSE_BOTTOM)
    {
        return TOUCH_CONTROL_CHEAT_CLOSE;
    }

    if (x < PLATFORM_CHEAT_GRID_LEFT || y < PLATFORM_CHEAT_GRID_TOP)
    {
        return TOUCH_CONTROL_NONE;
    }

    const int cell_step_x = PLATFORM_CHEAT_CELL_WIDTH +
                            PLATFORM_CHEAT_CELL_GAP_X;
    const int cell_step_y = PLATFORM_CHEAT_CELL_HEIGHT +
                            PLATFORM_CHEAT_CELL_GAP_Y;
    const int relative_x = x - PLATFORM_CHEAT_GRID_LEFT;
    const int relative_y = y - PLATFORM_CHEAT_GRID_TOP;
    const int column = relative_x / cell_step_x;
    const int row = relative_y / cell_step_y;

    if (column >= PLATFORM_CHEAT_GRID_COLUMNS || row >= 2 ||
        relative_x % cell_step_x >= PLATFORM_CHEAT_CELL_WIDTH ||
        relative_y % cell_step_y >= PLATFORM_CHEAT_CELL_HEIGHT)
    {
        return TOUCH_CONTROL_NONE;
    }

    const int cheat = row * PLATFORM_CHEAT_GRID_COLUMNS + column;
    return (touch_control_t)(TOUCH_CONTROL_CHEAT_0 + cheat);
}

static touch_control_t control_at(uint16_t x, uint16_t y)
{
    if (x >= PLATFORM_SCREEN_WIDTH || y >= PLATFORM_SCREEN_HEIGHT ||
        y < PLATFORM_CONTROLS_TOP)
    {
        return TOUCH_CONTROL_NONE;
    }

    if (platform_lcd_get_ui_mode() == PLATFORM_UI_WEAPONS)
    {
        return weapon_selector_control_at(x, y);
    }
    if (platform_lcd_get_ui_mode() == PLATFORM_UI_CHEATS)
    {
        return cheat_selector_control_at(x, y);
    }

    if (y >= PLATFORM_UTILITY_TOP)
    {
        if (x < PLATFORM_SOUND_TOUCH_RIGHT)
        {
            return TOUCH_CONTROL_SOUND;
        }
        if (x < PLATFORM_WEAPONS_TOUCH_RIGHT)
        {
            return weapon_selector_enabled
                       ? TOUCH_CONTROL_WEAPONS
                       : TOUCH_CONTROL_NONE;
        }
        if (x < PLATFORM_CHEATS_TOUCH_RIGHT)
        {
            return weapon_selector_enabled
                       ? TOUCH_CONTROL_CHEATS
                       : TOUCH_CONTROL_NONE;
        }
        return TOUCH_CONTROL_MENU;
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
        return x < PLATFORM_USE_STRAFE_SPLIT_X
                   ? TOUCH_CONTROL_USE
                   : TOUCH_CONTROL_STRAFE;
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

static void switch_ui_mode(platform_ui_mode_t mode)
{
    // Cancel movement/fire before hiding the gameplay controls, then ignore
    // every finger involved in this gesture until all contacts are released.
    queue_state_changes((touch_control_state_t){0});
    memset(touch_contacts, 0, sizeof(touch_contacts));
    pad_repeat_deadline = 0;
    platform_lcd_set_ui_mode(mode);
    suppress_until_all_contacts_up = true;
}

static void queue_key_sequence(const char *sequence)
{
    for (; *sequence; ++sequence)
    {
        send_event((event_t){
            .type = ev_keydown,
            .data1 = (uint8_t)*sequence,
        });
        send_event((event_t){
            .type = ev_keyup,
            .data1 = (uint8_t)*sequence,
        });
    }
}

static bool handle_touch_down(touch_control_t control)
{
    if (control == TOUCH_CONTROL_SOUND)
    {
        const bool muted = platform_audio_toggle_mute();
        platform_lcd_set_sound_muted(muted);
        return false;
    }

    if (control == TOUCH_CONTROL_STRAFE)
    {
        strafe_mode = !strafe_mode;
        platform_lcd_set_strafe_mode(strafe_mode);
        ESP_LOGI(TAG, "Strafe mode %s",
                 strafe_mode ? "enabled" : "disabled");
        return false;
    }

    if (control == TOUCH_CONTROL_WEAPONS && weapon_selector_enabled)
    {
        ESP_LOGI(TAG, "Weapon selector opened");
        switch_ui_mode(PLATFORM_UI_WEAPONS);
        return true;
    }

    if (control == TOUCH_CONTROL_CHEATS && weapon_selector_enabled)
    {
        ESP_LOGI(TAG, "Cheat selector opened");
        switch_ui_mode(PLATFORM_UI_CHEATS);
        return true;
    }

    if (control == TOUCH_CONTROL_WEAPON_CLOSE)
    {
        ESP_LOGI(TAG, "Weapon selector closed");
        switch_ui_mode(PLATFORM_UI_NORMAL);
        return true;
    }

    if (control == TOUCH_CONTROL_CHEAT_CLOSE)
    {
        ESP_LOGI(TAG, "Cheat selector closed");
        switch_ui_mode(PLATFORM_UI_NORMAL);
        return true;
    }

    const int weapon = weapon_index_for_control(control);
    if (weapon >= 0)
    {
        const uint16_t weapon_bit = (uint16_t)(1u << weapon);
        if (!weapon_selector_enabled ||
            !(available_weapon_mask & weapon_bit) ||
            !(owned_weapon_mask & weapon_bit))
        {
            ESP_LOGI(TAG, "Weapon slot %d unavailable", weapon + 1);
            return false;
        }

        ESP_LOGI(TAG, "Weapon slot %d selected", weapon + 1);
        switch_ui_mode(PLATFORM_UI_NORMAL);
        if (xQueueSend(weapon_request_queue, &weapon, 0) != pdTRUE)
        {
            ESP_LOGW(TAG, "Weapon request queue full; selection dropped");
        }
        return true;
    }

    const int cheat = cheat_index_for_control(control);
    if (cheat >= 0 && weapon_selector_enabled)
    {
        static const char *const sequences[PLATFORM_CHEAT_COUNT] = {
            "iddqd", "idkfa", "idfa",
            "idclip", "idbeholds", "idbeholdv",
        };
        ESP_LOGI(TAG, "Cheat slot %d: %s", cheat + 1,
                 sequences[cheat]);
        switch_ui_mode(PLATFORM_UI_NORMAL);
        queue_key_sequence(sequences[cheat]);
        return true;
    }

    return false;
}

static esp_err_t read_touch_packet(uint8_t data[TOUCH_PACKET_BYTES])
{
    static const uint8_t read_command[11] = {
        0xb5, 0xab, 0xa5, 0x5a,
        0x00, 0x00,
        (TOUCH_PACKET_BYTES >> 8) & 0xff,
        TOUCH_PACKET_BYTES & 0xff,
        0x00, 0x00, 0x00,
    };

    esp_err_t err = esp_lcd_panel_io_tx_param(touch_io, -1,
                                               read_command,
                                               sizeof(read_command));
    if (err == ESP_OK)
    {
        err = esp_lcd_panel_io_rx_param(touch_io, -1,
                                        data, TOUCH_PACKET_BYTES);
    }
    return err;
}

static bool pad_contact_active(void)
{
    for (int id = 0; id < TOUCH_ID_COUNT; ++id)
    {
        if (touch_contacts[id].active &&
            touch_contacts[id].control == TOUCH_CONTROL_PAD)
        {
            return true;
        }
    }
    return false;
}

static bool packet_has_active_contacts(
    const uint8_t data[TOUCH_PACKET_BYTES])
{
    const int records = data[1] < TOUCH_MAX_POINTS
                            ? data[1]
                            : TOUCH_MAX_POINTS;
    for (int point = 0; point < records; ++point)
    {
        const int offset = 2 + point * TOUCH_POINT_RECORD_BYTES;
        const uint8_t event_code = data[offset] >> 6;
        if (event_code == 0 || event_code == 2)
        {
            return true;
        }
    }
    return false;
}

static touch_control_state_t combined_touch_state(void)
{
    touch_control_state_t combined = {
        // Vanilla DOOM joystick button 1 switches horizontal movement from
        // turning to strafing. Keep it out of menus, where that same bit is
        // historically interpreted as Backspace.
        .buttons = strafe_mode && weapon_selector_enabled
                       ? JOYSTICK_BUTTON_STRAFE
                       : 0,
    };
    bool pad_already_combined = false;

    for (int id = 0; id < TOUCH_ID_COUNT; ++id)
    {
        const touch_contact_t *contact = &touch_contacts[id];
        if (!contact->active)
        {
            continue;
        }

        const touch_control_state_t state =
            controls_from_touch(contact->x, contact->y, contact->control);
        combined.buttons |= state.buttons;
        combined.escape = combined.escape || state.escape;

        // Only one navigation contact is expected. If two fingers start on
        // the pad, keep the lowest stable touch ID instead of oscillating.
        if (contact->control == TOUCH_CONTROL_PAD && !pad_already_combined)
        {
            combined.x = state.x;
            combined.y = state.y;
            pad_already_combined = true;
        }
    }

    return combined;
}

static void process_touch_sample(
    const uint8_t data[TOUCH_PACKET_BYTES])
{
    const uint8_t gesture = data[0];
    const uint8_t point_count = data[1];

    if (suppress_until_all_contacts_up)
    {
        if (!packet_has_active_contacts(data))
        {
            suppress_until_all_contacts_up = false;
            memset(touch_contacts, 0, sizeof(touch_contacts));
            ESP_LOGI(TAG, "UI transition released; touch input armed");
        }
        return;
    }

    bool seen_ids[TOUCH_ID_COUNT] = {false};
    const int records = point_count < TOUCH_MAX_POINTS
                            ? point_count
                            : TOUCH_MAX_POINTS;

    for (int point = 0; point < records; ++point)
    {
        const int offset = 2 + point * TOUCH_POINT_RECORD_BYTES;
        const uint8_t event_code = data[offset] >> 6;
        const uint8_t id = data[offset + 2] >> 4;

        // Event 0 is contact-down, event 2 is contact/drag. Event 1 is
        // lift-up and event 3 marks an unused record, so neither is active.
        if (event_code != 0 && event_code != 2)
        {
            continue;
        }

        const uint16_t x =
            (uint16_t)(((data[offset] & 0x0f) << 8) | data[offset + 1]);
        const uint16_t y =
            (uint16_t)(((data[offset + 2] & 0x0f) << 8) |
                       data[offset + 3]);
        touch_contact_t *contact = &touch_contacts[id];
        seen_ids[id] = true;

        if (!contact->active)
        {
            contact->active = true;
            contact->control = control_at(x, y);
            ESP_LOGI(TAG,
                     "Touch id=%u down raw=(%u,%u), gesture=%u, "
                     "points=%u -> %s",
                     (unsigned)id, (unsigned)x, (unsigned)y,
                     (unsigned)gesture, (unsigned)point_count,
                     control_name(contact->control));

            if (handle_touch_down(contact->control))
            {
                return;
            }
        }

        contact->x = x;
        contact->y = y;
    }

    // The controller compacts remaining records when either finger lifts.
    // Contact IDs, unlike record slots, remain stable, so unseen IDs are the
    // reliable way to release exactly the correct logical control.
    for (int id = 0; id < TOUCH_ID_COUNT; ++id)
    {
        if (touch_contacts[id].active && !seen_ids[id])
        {
            ESP_LOGI(TAG, "Touch id=%u up -> %s", (unsigned)id,
                     control_name(touch_contacts[id].control));
            touch_contacts[id] = (touch_contact_t){0};
        }
    }

    const touch_control_state_t state = combined_touch_state();
    const bool pad_active = pad_contact_active();
    const bool pad_direction_changed =
        pad_active &&
        (state.x != previous_state.x || state.y != previous_state.y);

    queue_state_changes(state);
    if (pad_direction_changed && (state.x != 0 || state.y != 0))
    {
        pad_repeat_deadline = xTaskGetTickCount() +
                              pdMS_TO_TICKS(PAD_REPEAT_DELAY_MS);
    }
    else if (!pad_active)
    {
        pad_repeat_deadline = 0;
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
    uint8_t data[TOUCH_PACKET_BYTES];

    while (true)
    {
        TickType_t wait_ticks = portMAX_DELAY;
        if (pad_contact_active() &&
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
            if (pad_contact_active() &&
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

bool platform_input_take_weapon_request(int *weapon)
{
    return weapon &&
           xQueueReceive(weapon_request_queue, weapon, 0) == pdTRUE;
}

void platform_input_set_weapon_state(uint16_t owned_mask,
                                     uint16_t available_mask,
                                     bool selector_enabled)
{
    owned_weapon_mask = owned_mask;
    available_weapon_mask = available_mask;
    weapon_selector_enabled = selector_enabled;

    if (!selector_enabled &&
        platform_lcd_get_ui_mode() != PLATFORM_UI_NORMAL)
    {
        platform_lcd_set_ui_mode(PLATFORM_UI_NORMAL);
    }
}

void platform_input_init(void)
{
    const i2c_master_bus_config_t bus_config = {
        .i2c_port = PLATFORM_TOUCH_I2C_PORT,
        .sda_io_num = PLATFORM_TOUCH_GPIO_SDA,
        .scl_io_num = PLATFORM_TOUCH_GPIO_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags = {
            .enable_internal_pullup = true,
        },
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &touch_i2c_bus));

    esp_lcd_panel_io_i2c_config_t io_config =
        ESP_LCD_TOUCH_IO_I2C_AXS15231B_CONFIG();
    io_config.scl_speed_hz = PLATFORM_TOUCH_I2C_HZ;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(touch_i2c_bus, &io_config, &touch_io));

    memset(&previous_state, 0, sizeof(previous_state));
    memset(touch_contacts, 0, sizeof(touch_contacts));
    suppress_until_all_contacts_up = false;
    strafe_mode = false;
    platform_lcd_set_strafe_mode(false);
    touch_event_queue = xQueueCreate(TOUCH_EVENT_QUEUE_LENGTH, sizeof(event_t));
    weapon_request_queue = xQueueCreate(2, sizeof(int));
    if (!touch_event_queue || !weapon_request_queue)
    {
        ESP_LOGE(TAG, "Unable to allocate input queues");
        abort();
    }

    if (xTaskCreate(touch_event_task, "touch_event", 3072, NULL, 5,
                    &touch_task_handle) != pdPASS)
    {
        ESP_LOGE(TAG, "Unable to create touch event task");
        abort();
    }

    const gpio_config_t interrupt_config = {
        .pin_bit_mask = 1ULL << PLATFORM_TOUCH_GPIO_INTERRUPT,
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
    ESP_ERROR_CHECK(gpio_isr_handler_add(PLATFORM_TOUCH_GPIO_INTERRUPT,
                                         touch_interrupt_handler, NULL));

    ESP_LOGI(TAG,
             "AXS15231B touch ready (%dx%d portrait, interrupt driven, "
             "%d points, packet=%d bytes)",
             PLATFORM_SCREEN_WIDTH, PLATFORM_SCREEN_HEIGHT,
             TOUCH_MAX_POINTS, TOUCH_PACKET_BYTES);
}
