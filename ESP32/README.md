# ESP32 firmware

[Version française](README.fr.md)

This directory is the PlatformIO/ESP-IDF project for the GUITION
JC3248W535 target. Start with the repository's [main README](../README.md)
for microSD preparation, flashing, controls, and legal information.

## Commands

Run from this directory:

```sh
pio run
pio run -t upload
pio device monitor --baud 115200
```

Or from the repository root:

```sh
pio run -d ESP32
pio run -d ESP32 -t upload
```

The only current environment is `jc3248w535`. It pins the Espressif Platform
version used by the tested build and selects the custom 4 MiB application
partition.

## Source boundaries

- `src/i_*.c`: implementations of DOOM's historical platform interfaces.
- `src/platform/`: board services, hardware definition, touch decoding, and
  the custom control panel.
- `components/doom/`: compiles `../linuxdoom-1.10` while excluding the
  original PC `i_*` backends.
- `components/doom_music/`: vendored MUS parser and OPL emulators.

See [Architecture](../docs/ARCHITECTURE.md) for the full data flow and
[Hardware](../docs/HARDWARE.md) for the tested pin map. The current limits and
planned path toward multiple board backends are documented in
[Porting](../docs/PORTING.md).
