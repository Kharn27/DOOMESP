#ifndef PLATFORM_INPUT_H
#define PLATFORM_INPUT_H

#include <stdbool.h>
#include <stdint.h>

#include "d_event.h"
#include "d_ticcmd.h"

int platform_input_read(event_t *event, ticcmd_t *ticcmd);
bool platform_input_take_weapon_request(int *weapon);
void platform_input_init(void);
void platform_input_set_weapon_state(uint16_t owned_mask,
                                     uint16_t available_mask,
                                     bool selector_enabled);

#endif
