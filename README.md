# Manul

Text web browser for Raspberry Pi Pico 2 (RP2350) with HDMI output, PS/2 keyboard, and WiFi via ESP-01 module.

## Features

- HTTP and HTTPS browsing
- Streaming HTML parser with text rendering
- Keyboard-driven navigation
- WiFi configuration UI
- UTF-8 Cyrillic support (Win-1251 font)
- Link highlighting, scrolling, history
- Redirect following (301/302/307/308)
- IRQ-buffered PIO UART for reliable ESP-01 communication

## Supported Board

This firmware is designed for the **M2** GPIO layout on RP2350-based boards with integrated HDMI, PS/2 keyboard, I2S audio, and ESP-01 WiFi:

- **[FRANK](https://rh1.tech/projects/frank?area=about)** -- A versatile development board based on RP Pico 2, HDMI output, and extensive I/O options.

## Hardware Requirements

- **Raspberry Pi Pico 2** (RP2350) or compatible board with M2 GPIO layout
- **HDMI connector** (directly connected via resistors, no encoder needed)
- **PS/2 keyboard**
- **ESP-01 WiFi module** running [frank-netcard](https://github.com/rh1tech/frank-netcard) AT command firmware
- **I2S DAC module** (optional, for audio feedback)

### Wiring (M2 Layout)

#### HDMI (via 270 ohm resistors)
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

#### PS/2 Keyboard
| Signal | GPIO |
|--------|------|
| CLK    | 2    |
| DATA   | 3    |

#### PS/2 Mouse
| Signal | GPIO |
|--------|------|
| CLK    | 0    |
| DATA   | 1    |

#### ESP-01 WiFi (PIO UART)
| Signal     | GPIO |
|------------|------|
| TX (to ESP RX)  | 21   |
| RX (from ESP TX) | 20   |

#### I2S Audio
| Signal | GPIO |
|--------|------|
| DATA   | 9    |
| BCLK   | 10   |
| LRCLK  | 11   |

## Controls

| Key | Action |
|-----|--------|
| Up / Down | Scroll one line |
| PgUp / PgDn | Scroll one page |
| Home / End | Top / bottom of page |
| Tab | Select next link |
| Shift+Tab | Select previous link |
| Enter | Follow selected link |
| Backspace | Go back in history |
| Ctrl+L | Enter URL |
| Escape | Cancel loading / exit URL input |
| F1 | Help |
| F2 | WiFi setup |
| F5 | Reload page |

## Building

### Prerequisites

1. Install the [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk) (version 2.2+)
2. Set environment variable: `export PICO_SDK_PATH=/path/to/pico-sdk`
3. Install ARM GCC toolchain

### Build Steps

```bash
git clone https://github.com/rh1tech/frank-manul.git
cd frank-manul

# DispHSTX display library (required)
git clone https://github.com/Panda381/DispHSTX.git lib/DispHSTX

./build.sh
```

Or manually:

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### Flashing

```bash
# With device in BOOTSEL mode:
picotool load build/frank_manul.uf2

# Or with device running:
./flash.sh
```

### Release Builds

```bash
./release.sh
```

Creates a versioned UF2 file in the `release/` directory.

## ESP-01 Setup

The ESP-01 module must be running the [frank-netcard](https://github.com/rh1tech/frank-netcard) AT command firmware. This firmware provides:

- WiFi scanning and connection
- TCP and TLS socket support
- DNS resolution

See the frank-netcard repository for flashing instructions.

## License

GNU General Public License v3. See [LICENSE](LICENSE) for details.

Hardware drivers based on [iris-2350](https://github.com/rh1tech/iris-2350) (GPL-3.0).
Display library: [DispHSTX](https://github.com/Panda381/DispHSTX) by Miroslav Nemecek (permissive license).

## Author

Mikhail Matveev <xtreme@rh1.tech>
