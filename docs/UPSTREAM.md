# LinuxDOOM upstream and local changes

## Provenance

The engine is based on id Software's official
[DOOM source release](https://github.com/id-Software/DOOM), specifically the
`linuxdoom-1.10` tree. The original release notes remain in `README.TXT` and
the GPL version 2 text remains in `LICENSE.TXT`.

The repository keeps the historical source directory intact rather than
importing those files into the ESP-IDF application component. This makes the
upstream origin visible and keeps ordinary platform replacements (`i_video`,
`i_sound`, and `i_net`) outside the engine.

## Build selection

`ESP32/components/doom/CMakeLists.txt` compiles the engine's C sources but
removes the original PC implementations:

- `i_main.c`
- `i_net.c`
- `i_sound.c`
- `i_video.c`

Their ESP32 counterparts live in `ESP32/src`. `i_system.c` remains part of
the engine because it owns the original zone allocator, clock, and fatal-error
contract; its target-specific branches use ESP-IDF services.

## Intentional engine patches

Changes inside `linuxdoom-1.10` fall into four groups.

### Portability and compiler correctness

The 1997 code relies on several implicit-int declarations, signed plain
`char`, old pointer conversions, and expression side effects rejected or
miscompiled by modern embedded toolchains. The local tree makes those types
and operations explicit while preserving the game protocol and behavior.

### Embedded memory and timing

- `doomtype.h` provides the `DOOM_EXT_RAM_BSS` annotation.
- `i_system.c` places the 6 MiB DOOM zone and low allocations in PSRAM,
  derives 35 Hz tics from the ESP32 millisecond clock, uses FreeRTOS delays,
  and stops safely after fatal errors instead of exiting a nonexistent OS.
- Large static tables that do not require internal RAM can use the external
  RAM annotation.

### Filesystem and IWAD handling

- `w_wad.c` routes file operations through the mounted platform filesystem,
  handles partial reads, avoids large stack allocations, and validates WAD
  directory/lump bounds.
- `d_main.c` searches `/sdcard` for supported IWADs and stores the default
  configuration there.

### Handheld presentation and controls

- `d_main.c` and `i_video.h` add the narrowly scoped detached status-bar
  hooks used by the portrait layout.
- `m_misc.c` defaults to and enforces the full 320 x 200 viewport so the
  separate live status bar is always visible.
- `m_menu.c` accepts the event-driven touch joystick without the delay used
  for continuously polled desktop joysticks.
- Demo handling accepts the bundled 1.9 demo format used by common IWADs.

## Patch policy

Contributions should keep platform logic out of the engine whenever an
existing `I_*` boundary can express it. If an engine change is necessary:

1. Keep it minimal.
2. Guard target-only behavior with `DOOM_ESP32` where practical.
3. Explain why an `I_*` or platform-layer solution is insufficient.
4. Preserve demo/gameplay semantics unless the change fixes a documented
   portability bug.
5. Build the complete `jc3248w535` firmware before submitting.

This policy is more maintainable than carrying a separate opaque fork or a
large patch applied during every build.

