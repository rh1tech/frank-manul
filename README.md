# Manul

Official page: **[frank.rh1.tech](https://frank.rh1.tech/)** — hub for all FRANK boards and firmware.

Text web browser for Raspberry Pi Pico 2 (RP2350) with HDMI output, PS/2 keyboard, and WiFi via ESP-01 module.

Renders HTML pages to an 80x30 character grid over HDMI. Connects to the internet through an ESP-01 running the [frank-netcard](https://github.com/rh1tech/frank-netcard) AT modem firmware.

## What it does

- Browses HTTP and HTTPS sites
- Parses HTML as it arrives (no full-page buffering)
- Follows redirects (301/302/307/308)
- Displays Cyrillic text (Win-1251 font with UTF-8 decoding)
- Stores WiFi credentials in flash

## Supported board

Uses the M2 GPIO layout. Tested on the [FRANK](https://rh1.tech/projects/frank?area=about) board (RP2350-based, HDMI, PS/2, I2S audio).

## Hardware

- RP2350 board with M2 GPIO layout
- HDMI connector (directly connected via resistors)
- PS/2 keyboard
- ESP-01 WiFi module running [frank-netcard](https://github.com/rh1tech/frank-netcard)
- I2S DAC (optional, for beeps)

### Wiring (M2 layout)

HDMI (via 270 ohm resistors):

| Signal | GPIO |
|--------|------|
| CLK-   | 12   |
| CLK+   | 13   |
| D0-    | 14   |
| D0+    | 15   |
| D1-    | 16   |
| D1+    | 17   |
| D2-    | 18   |
| D2+    | 19   |

PS/2 Keyboard: CLK=2, DATA=3
PS/2 Mouse: CLK=0, DATA=1
ESP-01: TX=21 (to ESP RX), RX=20 (from ESP TX)
I2S Audio: DATA=9, BCLK=10, LRCLK=11

## Controls

| Key | Action |
|-----|--------|
| Up / Down | Scroll one line |
| PgUp / PgDn | Scroll one page |
| Home / End | Top / bottom of page |
| Tab | Next link |
| Shift+Tab | Previous link |
| Enter | Follow link |
| Backspace | Back |
| Ctrl+L | Enter URL |
| Escape | Cancel / exit URL input |
| F1 | Help |
| F2 | WiFi setup |
| F5 | Reload |

## Building

You need the [Pico SDK](https://github.com/raspberrypi/pico-sdk) (2.2+) and ARM GCC.

```bash
git clone https://github.com/rh1tech/frank-manul.git
cd frank-manul

# Display library (required, not included)
git clone https://github.com/Panda381/DispHSTX.git lib/DispHSTX

./build.sh
```

### Flashing

```bash
# BOOTSEL mode:
picotool load build/frank_manul.uf2

# Or with device running:
./flash.sh
```

## ESP-01 setup

The ESP-01 must run the [frank-netcard](https://github.com/rh1tech/frank-netcard) firmware, which turns it into an AT command modem (WiFi, TCP, TLS, DNS). See that repo for flashing instructions.

## License

GPL-3.0. See [LICENSE](LICENSE).

Hardware drivers from [iris-2350](https://github.com/rh1tech/iris-2350) (GPL-3.0).
Display library: [DispHSTX](https://github.com/Panda381/DispHSTX) by Miroslav Nemecek (permissive).

## Author

Mikhail Matveev <xtreme@rh1.tech>
