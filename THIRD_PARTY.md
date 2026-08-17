# Third-party software and assets

## LinuxDOOM 1.10

- Source: [id-Software/DOOM](https://github.com/id-Software/DOOM)
- Copyright: id Software and the copyright holders named in the source
- License: GNU GPL version 2; see `LICENSE.TXT`

No commercial DOOM IWAD, PWAD, music, sound, or other game data is included.
Users must provide their own legally obtained compatible IWAD.

## Espressif AXS15231B component

- Package: `espressif/esp_lcd_axs15231b` 2.1.0
- Source: [ESP Component Registry](https://components.espressif.com/components/espressif/esp_lcd_axs15231b/versions/2.1.0)
- License: Apache-2.0

The component is downloaded by ESP-IDF's component manager and is not
vendored in this repository. Its resolved dependency graph is recorded in
`ESP32/dependencies.lock`.

## LittleMUS

- Vendored in: `ESP32/components/doom_music/musplayer.*`
- Upstream revision recorded by the component: `c551f1fba021343bc54f06381d828d022461f223`
- Copyright: Andrew Towers
- License: MIT; see `ESP32/components/doom_music/LICENSE.LittleMUS`

## Woody-OPL

- Vendored in: `ESP32/components/doom_music/woody_opl.*`
- Upstream revision recorded by the component: `c3f6674e4394fd9a83fe52722cfc63e1a9a8e29c`
- Copyright: DOSBox Team, Ken Silverman, and contributors
- License: LGPL-2.1-or-later; see
  `ESP32/components/doom_music/LICENSE.Woody-OPL`

Woody-OPL is the emulator compiled into the current firmware.

## Nuked OPL3

- Vendored in: `ESP32/components/doom_music/opl3.*`
- Copyright: Nuke.YKT and contributors
- License: LGPL-2.1-or-later; see
  `ESP32/components/doom_music/LICENSE.Nuked-OPL3`

This alternative emulator is retained for experimentation but is not part of
the current component build.

## Documentation media

Photos and videos under `docs/media` were captured during development of this
port and are not part of the GPL-licensed program source. They contain visual
output produced from user-supplied DOOM game data. All game names, artwork,
and trademarks remain the property of their respective owners.

