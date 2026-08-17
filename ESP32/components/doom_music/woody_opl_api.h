#pragma once

#include <stdint.h>

// Narrow API for the single global Woody-OPL instance. The upstream header
// contains the emulator state definitions and must only be included by its
// implementation file.
void woody_opl_init(uint32_t sample_rate);
void woody_opl_write(uintptr_t reg, uint8_t value);
void woody_opl_getsample(int16_t *samples, intptr_t frame_count);
