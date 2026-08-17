# Troubleshooting

Start the serial monitor at 115200 baud and reset the board. The log reports
flash/PSRAM discovery, LCD initialization, SD status, IWAD selection, memory,
touch, and audio initialization.

```sh
pio device monitor --port /dev/ttyACM0 --baud 115200
```

Replace the port as needed. Close the monitor before uploading because only
one process can own the serial port.

## Upload port is missing or busy

- Use a data-capable USB cable.
- Reconnect the board and check Device Manager or `ls /dev/ttyACM*`.
- Close every serial monitor before uploading.
- Specify `--upload-port /dev/ttyACM0` or the appropriate Windows `COM` port.
- On Linux, install PlatformIO's udev rules or ensure the user has permission
  to access the device.

## `sdmmc_card_init failed` / `ESP_ERR_TIMEOUT`

The card did not answer the SDSPI initialization sequence. This happens
before the filesystem or WAD is read.

1. Fully insert the card.
2. Power-cycle the board rather than only restarting the monitor.
3. Try another microSD card.
4. Confirm the PCB is marked `JC3248W535`.

## Card answers but FAT cannot be mounted

Reformat the card as FAT32. The firmware never formats a card automatically,
so a mount failure cannot erase its contents.

## `No IWAD found in /sdcard`

Place one supported file at the card root, not in a subdirectory:

```text
doom2f.wad  doom2.wad  plutonia.wad  tnt.wad
doomu.wad   doom.wad   doom1.wad
```

Use a legally obtained IWAD and the exact lowercase filename. A WAD version
number is not the filename: for example, The Ultimate DOOM normally uses
`doomu.wad`.

## Lit black screen

Check the serial log first. A lit but empty panel often means the LCD started
and the application then stopped on SD or IWAD initialization. If the log
continues into `D_DoomMain` but the image remains black, confirm the exact
board revision and attach the complete boot log to an issue.

## Glitches, duplicated bands, or a shifted image

This target requires the JC3248W535-specific AXS15231B initialization and
sequential QSPI strip behavior. Do not substitute a generic AXS15231B setup.
Confirm that `platform = espressif32@6.12.0` and component version 2.1.0 are
being used, then perform a clean build:

```sh
pio run -d ESP32 -t clean
pio run -d ESP32
```

## Touch is rotated or does not match the artwork

The current mapping assumes the panel's native 320 x 480 portrait
orientation. A mismatch strongly suggests another board/panel revision.
Include raw touch coordinates from the serial log and a photo of the PCB in
the issue report.

## Two controls work, but a third finger does not

That is a hardware limit of the tested touch controller configuration. Use
the persistent `STF` toggle so forward + sidestep + fire needs only two
contacts.

## No sound

- Connect a speaker to the amplified speaker output, never directly to an
  ESP32 GPIO.
- Tap the speaker icon and confirm it is not muted.
- Check sound-effect and music volumes in DOOM's menu.
- Look for `NS4168 ready` and `GENMIDI OPL instrument bank ready` in the log.
- An 8 ohm, 1.5 W speaker is the tested reference.

## Build dependency errors

The first build needs internet access. Delete neither `dependencies.lock` nor
the version pins while diagnosing. If the managed component cache is damaged,
remove `ESP32/managed_components` and rebuild; ESP-IDF will download the
locked dependencies again.

## Reporting a crash

Copy the complete log from reset through the backtrace. Keep the matching
`.pio/build/jc3248w535/firmware.elf`; the PlatformIO exception decoder uses it
to translate addresses into source locations. Never attach commercial WAD
files to an issue.

