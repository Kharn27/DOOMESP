# Changelog

[Version française](CHANGELOG.fr.md)

This project is still pre-release. Entries describe the current `main` branch
until the first versioned release is tagged.

## Unreleased

### Added

- Native JC3248W535 display, capacitive touch, microSD, and PSRAM support.
- Portrait 320 x 200 gameplay with a detached live DOOM status bar.
- Two-point multitouch controls and persistent strafe mode.
- Weapon and cheat selection panels using WAD assets.
- Eight-channel sound effects, MUS/OPL3 music, and runtime mute.
- 4 MiB application partition.
- Central board hardware definition.
- Public build, architecture, hardware, troubleshooting, licensing, and
  contribution documentation.

### Fixed

- Weapon and cheat icons no longer disappear after extended demo playback or
  level changes. Selector artwork now has a stable, deduplicated PSRAM cache
  independent from DOOM's purgeable WAD cache.

### Known limitations

- Single-player only; no Wi-Fi or Bluetooth gameplay yet.
- Only the GUITION board marked `JC3248W535` has been validated.
- Save-game behavior has not yet been validated as part of the public test
  matrix.
