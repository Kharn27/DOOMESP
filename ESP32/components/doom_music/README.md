# DOOM music support

This component contains three vendored upstream libraries:

- LittleMUS, commit `c551f1fba021343bc54f06381d828d022461f223`
  (MIT, see `LICENSE.LittleMUS`)
- Woody-OPL, commit `c3f6674e4394fd9a83fe52722cfc63e1a9a8e29c`
  (LGPL-2.1-or-later, see `LICENSE.Woody-OPL`)
- Nuked OPL3 (LGPL-2.1-or-later, see `LICENSE.Nuked-OPL3`)

LittleMUS converts the WAD's MUS event stream and GENMIDI instrument bank to
OPL register writes. Woody-OPL turns those writes into PCM samples.
Nuked OPL3 is retained as an alternative for experimentation but is not
compiled by the current `CMakeLists.txt`.

Local changes:

- The three OPL reset loops in `musplay_start()` use `ch < 9`; upstream used
  `ch <= 9`, which indexed past the nine-entry operator mapping arrays.
- Woody-OPL uses `float` instead of `double`, matching the ESP32-S3 hardware
  FPU, and a 128-frame work block to reduce task stack and static RAM use.
- Woody-OPL's temporary sample tables are persistent because the port has one
  dedicated audio instance; this avoids placing them on the FreeRTOS stack.
- Woody-OPL's public entry points are prefixed to avoid colliding with the
  LittleMUS `adlib_write()` callback.
