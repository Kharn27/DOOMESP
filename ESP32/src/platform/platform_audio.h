#ifndef PLATFORM_AUDIO_H
#define PLATFORM_AUDIO_H

#include <stdbool.h>

// Toggles the final mixed output while keeping the game, music, and sound
// effect timelines running. Returns the new muted state.
bool platform_audio_toggle_mute(void);

#endif
