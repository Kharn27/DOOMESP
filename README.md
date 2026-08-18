# DOOMESP

[Documentation française](README.fr.md)

[![Build firmware](https://github.com/Kharn27/DOOMESP/actions/workflows/build.yml/badge.svg)](https://github.com/Kharn27/DOOMESP/actions/workflows/build.yml)
[![License: GPL-2.0](https://img.shields.io/badge/license-GPL--2.0-blue.svg)](LICENSE.TXT)
[![Target: ESP32-S3](https://img.shields.io/badge/target-ESP32--S3-red.svg)](docs/HARDWARE.md)

DOOM running natively on the **GUITION JC3248W535**: an ESP32-S3 board with
a 320 x 480 AXS15231B QSPI capacitive touchscreen, 16 MiB of flash, 8 MiB of
octal PSRAM, a microSD slot, and an NS4168 audio amplifier.

This is a source port of the official LinuxDOOM 1.10 release. It renders the
original 320 x 200 game at the top of the portrait display, moves DOOM's live
status bar directly below it, and uses the remaining pixels for a two-point
touch interface.

<p align="center">
  <img src="docs/media/gameplay.jpg" width="300" alt="DOOM gameplay on the JC3248W535 with the current touch controls">
  <img src="docs/media/main-menu.jpg" width="300" alt="DOOM's original menu above the JC3248W535 touch controls">
</p>

<p align="center">
  <img src="docs/media/weapon-selector.jpg" width="300" alt="Touch weapon selector using sprites from the loaded WAD">
  <img src="docs/media/cheat-selector.jpg" width="300" alt="Touch cheat selector using sprites from the loaded WAD">
</p>

> The footage shows an active development build; small details of the touch
> layout may continue to evolve. Watch the
> [gameplay demo](https://cdn.jsdelivr.net/gh/Kharn27/DOOMESP@main/docs/media/gameplay-demo.mp4)
> and the
> [touch controls and weapon selector](https://cdn.jsdelivr.net/gh/Kharn27/DOOMESP@main/docs/media/touch-controls.mp4).

## What works

- Smooth 320 x 200 software rendering on the 320 x 480 portrait display.
- Original, live 320 x 32 DOOM status bar detached from the game viewport.
- Capacitive two-point multitouch: move and fire at the same time.
- Direction pad, fire, use, persistent strafe mode, and menu controls.
- WAD-powered weapon selector with owned/unowned visual states.
- Touch cheat panel for the classic single-player cheats.
- Eight-channel sound-effect mixer through the on-board NS4168 amplifier.
- MUS music playback with OPL3 synthesis.
- Runtime mute control.
- IWAD and configuration storage on FAT32 microSD.

Networking is not implemented yet; this build is single-player only.

## Required hardware

- GUITION **JC3248W535** board. Check the marking on the PCB; similarly sized
  AXS15231B boards can use different pin assignments.
- FAT32-formatted microSD card.
- Data-capable USB cable.
- Optional speaker connected to the board's amplified speaker output. An
  8 ohm, 1.5 W speaker has been tested successfully.
- A legally obtained compatible IWAD.

The exact buses and GPIOs are listed in [the hardware guide](docs/HARDWARE.md)
and centralized in
[`platform_board.h`](ESP32/src/platform/platform_board.h).

## Quick start

### 1. Prepare the microSD card

Format the card as FAT32 and copy **one** supported IWAD to its root. File
names are case-sensitive on some cards, so use lowercase:

| Search order | File | Game |
| ---: | --- | --- |
| 1 | `doom2f.wad` | DOOM II French |
| 2 | `doom2.wad` | DOOM II |
| 3 | `plutonia.wad` | Final DOOM: Plutonia |
| 4 | `tnt.wad` | Final DOOM: TNT |
| 5 | `doomu.wad` | The Ultimate DOOM |
| 6 | `doom.wad` | Registered DOOM |
| 7 | `doom1.wad` | Shareware DOOM |

The project intentionally contains **no WAD or other commercial game data**.
The firmware reads lumps from the card as needed and creates
`/default.cfg` there.

### 2. Build and upload with VS Code

1. Install [Visual Studio Code](https://code.visualstudio.com/) and the
   [PlatformIO IDE extension](https://docs.platformio.org/en/latest/integration/ide/vscode.html).
2. Clone this repository and open `DOOMESP.code-workspace`.
3. Open `ESP32/platformio.ini` so PlatformIO activates the project.
4. Select the `jc3248w535` environment.
5. Connect the board, then run **PlatformIO: Upload**.
6. Insert the prepared card and reset the board.

The first build downloads ESP-IDF and Espressif's AXS15231B component, so it
requires an internet connection and takes longer than subsequent builds.

### 3. Build and upload from a terminal

With [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation/index.html)
installed, run these commands from the repository root:

```sh
pio run -d ESP32
pio run -d ESP32 -t upload
pio device monitor --baud 115200
```

If port detection fails, specify it explicitly. For example:

```sh
pio run -d ESP32 -t upload --upload-port /dev/ttyACM0
pio device monitor --port /dev/ttyACM0 --baud 115200
```

Use the corresponding `COM` port on Windows.

## Touch controls

| Control | Action |
| --- | --- |
| D-pad up/down | Move forward/backward |
| D-pad left/right | Turn, or sidestep while `STF` is active |
| `FIRE` | Fire; also confirm in DOOM menus |
| `USE` | Open doors and activate switches |
| `STF` | Toggle persistent strafe mode; blue means active |
| Speaker | Mute/unmute sound effects and music |
| `WEAP` | Open the weapon selector |
| `CHEAT` | Open the cheat panel |
| `MENU` | Open/close DOOM's original menu |

The touch controller reports at most two contacts. Persistent `STF` therefore
allows forward + sidestep + fire without requiring a third finger.

## Repository layout

```text
DOOMESP/
├── linuxdoom-1.10/          Official engine sources plus the ESP32 patch set
├── ESP32/
│   ├── components/doom/     ESP-IDF build wrapper for the engine
│   ├── components/doom_music/
│   │                        Vendored MUS and OPL emulation
│   └── src/
│       ├── i_*.c            DOOM platform backends
│       └── platform/        JC3248W535 hardware services and touch UI
├── docs/                    Architecture, hardware and troubleshooting
└── README.TXT               Original id Software release notes
```

The split follows DOOM's original portability model instead of forking the
whole engine into ESP-specific code. See [Architecture](docs/ARCHITECTURE.md)
and [Upstream changes](docs/UPSTREAM.md) for the detailed boundaries.

## Documentation

- [Architecture and data flow](docs/ARCHITECTURE.md)
- [Hardware, pins, memory, and audio](docs/HARDWARE.md)
- [Build and runtime troubleshooting](docs/TROUBLESHOOTING.md)
- [Upstream source and local engine changes](docs/UPSTREAM.md)
- [Third-party components and licenses](THIRD_PARTY.md)
- [Contributing](CONTRIBUTING.md)
- [Changelog](CHANGELOG.md)

## Legal

The source code is distributed under the GNU GPL version 2; see
[LICENSE.TXT](LICENSE.TXT). DOOM game data is not covered by that source
license and is not included. You must supply your own legally obtained IWAD.

DOOM is a trademark of its respective owner. This community project is not
affiliated with or endorsed by id Software, Bethesda, or ZeniMax.
