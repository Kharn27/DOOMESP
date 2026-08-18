# Contributing

[Version française](CONTRIBUTING.fr.md)

Issues, hardware validation, documentation improvements, and code changes are
welcome.

## Before opening an issue

- Confirm the PCB is marked `JC3248W535`.
- Build the current `main` branch.
- Read `docs/TROUBLESHOOTING.md`.
- Capture the serial log from reset at 115200 baud.
- Do not upload or link commercial WAD files.

For display or touch problems, include a clear PCB photo and the raw touch
coordinates printed by the firmware. For crashes, keep the matching ELF and
include the complete backtrace.

## Development setup

Install PlatformIO, clone the repository, and run:

```sh
pio run -d ESP32
```

The expected result is a successful `jc3248w535` release build. Hardware
changes should also be flashed and tested on the physical board.

## Source guidelines

- Preserve the original LinuxDOOM architecture and `I_*` interfaces.
- Put general board services under `ESP32/src/platform`.
- Keep physical pin assignments in `platform_board.h`.
- Keep touch geometry in `platform_controls.h`.
- Avoid editing `linuxdoom-1.10` when the behavior can live in an ESP32
  backend. Follow `docs/UPSTREAM.md` when an engine patch is necessary.
- Match the surrounding C style; port code uses four-space indentation.
- Keep interrupt handlers short and keep LCD transactions on the render task.
- Prefer PSRAM for large caches and internal/DMA memory for time-critical
  transfer buffers.

## Pull requests

Describe:

1. The problem and intended behavior.
2. The tested board revision and WAD family (never attach the WAD).
3. Build result and firmware size.
4. Physical test result for display, touch, SD, or audio changes.
5. Any changes made inside `linuxdoom-1.10` and why they were unavoidable.

Contributions to this GPL-2.0 project must be compatible with the repository's
license. Retain the licenses and attribution of vendored third-party code.
