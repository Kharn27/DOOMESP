# Porting to another ESP32-S3 board

[Version française](PORTING.fr.md)

DOOMESP currently has one supported and physically validated target: the
GUITION JC3248W535. Supporting more ESP32-S3 boards is a project direction,
not a capability that `platform_board.h` already provides on its own.

The goal is to keep LinuxDOOM and the DOOM-facing `I_*` backends independent
from the selected display, touch controller, storage bus, and audio hardware.
New ports should extend the ESP32 platform layer instead of adding board
conditions to the game engine.

## What is configurable today

`ESP32/src/platform/platform_board.h` centralizes the tested GPIOs, bus hosts,
and clock rates. Changing it can be sufficient only for a board that keeps the
same hardware topology:

- AXS15231B 320 x 480 QSPI display;
- AXS15231B two-point touch over I2C;
- microSD over SDSPI;
- standard I2S audio output;
- compatible flash and octal PSRAM configuration.

Even in that favorable case, the panel initialization sequence, orientation,
touch coordinates, and bus stability must be verified on the physical board.
Do not replace the JC3248W535 values to add another target: give the new board
its own PlatformIO environment and, once board selection exists, its own board
profile.

## Current hardware coupling

| Concern | Current implementation | What another target may require |
| --- | --- | --- |
| Pins and clocks | `platform_board.h` | A separate board profile |
| Flash and PSRAM | `platformio.ini`, `sdkconfig.defaults` | A board definition, memory mode, and partition table |
| LCD controller | `platform_lcd.c`, Espressif AXS15231B component | Another display backend and component dependency |
| Panel timing | JC3248W535 command table in `platform_lcd.c` | A sequence verified for the exact panel |
| Resolution and UI | `platform_controls.h`, currently fixed at 320 x 480 | New layout geometry and possibly scaling or centering |
| Touch transport | AXS15231B packet decoder in `platform_input.c` | Another touch backend and coordinate transform |
| Storage | SDSPI implementation in `platform_fs.c` | SDMMC, SPI flash, or another filesystem backend |
| Audio | Standard I2S path in `i_sound.c` | Different pins only, or a new DAC/PDM/audio backend |

The original engine always produces a 320 x 200 game image. Its detached
status bar is 320 x 32. A display whose width is not 320 pixels therefore
needs an explicit scaling, centering, or cropping policy in the ESP32 display
path; it does not require a renderer rewrite in `linuxdoom-1.10`.

## Intended abstraction boundary

The current files deliberately proved the complete device before introducing
several interfaces at once. A future multi-board layout should separate the
physical drivers from the touch UI and game-facing behavior, approximately as
follows:

```text
ESP32/src/platform/
├── boards/
│   ├── jc3248w535.h
│   └── another_board.h
├── display/
│   ├── platform_display.h
│   └── display_axs15231b.c
├── touch/
│   ├── platform_touch.h
│   └── touch_axs15231b.c
├── platform_ui.c
├── platform_fs.c
└── platform_audio.c
```

This tree is a design direction, not a frozen API or a description of files
that already exist. The useful contracts are more important than the exact
names:

- A display backend initializes the controller and presents RGB565 regions.
- A touch backend reports contact IDs and coordinates in one documented,
  canonical screen orientation.
- The UI layer draws controls and maps those contacts to logical DOOM actions;
  it must not decode controller-specific packets.
- Storage exposes files to DOOM without making the engine know whether they
  came from SDSPI, SDMMC, or another supported medium.
- Audio consumes the final PCM stream without exposing the amplifier or bus
  choice to the mixer.
- Board selection happens at compile time. Embedded targets do not need
  runtime probing and should not pay for unused drivers.

## Recommended migration sequence

Abstraction should be introduced incrementally while bringing up a real
second board:

1. Record the exact module, flash, PSRAM, display, touch, SD, audio, power, and
   pin specifications for the new target.
2. Add a named PlatformIO environment; keep `jc3248w535` as the tested default.
3. Introduce compile-time board selection and move the existing values into a
   JC3248W535 profile without changing its behavior.
4. Extract the AXS15231B LCD transport from control-panel drawing, then add the
   new display backend or panel profile.
5. Extract raw touch acquisition from logical control mapping, then normalize
   the new controller's coordinates and contact lifecycle.
6. Adapt the layout only after the physical resolution and orientation are
   stable.
7. Add storage and audio variants only when the new board actually needs them.
8. Build every supported PlatformIO environment in CI and physically retest
   the hardware affected by the change.

This order keeps the working device as a regression target and prevents a
speculative abstraction from being designed around only one implementation.

## Port validation checklist

A board should not be described as supported until the following have been
tested on physical hardware:

- cold boot and repeated reset;
- flash and PSRAM detection, allocation headroom, and sustained gameplay;
- full-screen solid colors followed by prolonged animated LCD updates;
- display orientation, byte order, raster timing, and backlight polarity;
- raw touch corners, stable contact IDs, release events, and simultaneous
  movement plus firing;
- FAT mount and sustained WAD reads across level and demo changes;
- simultaneous sound effects and music without underruns;
- menu navigation, status-bar updates, weapon selection, and cheat selection;
- a release build plus a complete serial log from reset.

Ports with less PSRAM may be possible, but the current implementation places
the DOOM zone, framebuffers, UI assets, and audio caches in external RAM. Such
a target needs measurement and memory work; changing its pin map alone is not
enough.

## Future candidate: GUITION JC4880P443

> **TODO — port not started.** The JC3248W535 remains the only supported and
> physically validated target. No P4 source files, build environment, or
> compatibility claim exists yet.

A GUITION **JC4880P443** is available as a possible second hardware target
once the current ESP32-S3 port is complete and stable. The available unit is
advertised with:

- an ESP32-P4 application processor and ESP32-C6 wireless companion;
- a 4.3-inch MIPI display;
- 16 MiB of flash and 32 MiB of PSRAM;
- capacitive touch, microSD, speaker and microphone connections;
- USB 2.0 High-Speed/Full-Speed ports;
- an integrated 2-megapixel camera and MIPI-CSI connection.

This board is intentionally recorded as a future candidate rather than an
active milestone. When work eventually begins, its first scope should be
limited to the P4, display, touch, storage, and audio needed by DOOM. The C6
wireless link, camera, H.264/JPEG facilities, battery support, and other board
expansion features should remain out of scope until the basic game port is
validated.

The architectural gap is useful: its RISC-V target, MIPI display path, larger
memory topology, and separate radio processor will test whether the hardware
boundaries described above are genuine. That refactoring must nevertheless be
driven by the future bring-up work and must preserve the JC3248W535 as the
default regression target.
