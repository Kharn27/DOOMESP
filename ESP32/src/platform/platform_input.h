#ifndef PLATFORM_INPUT_H
#define PLATFORM_INPUT_H

#include "d_event.h"
#include "d_ticcmd.h"

int platform_input_read(event_t *event, ticcmd_t *ticcmd);
void platform_input_init(void);

#endif
