# Wiring / Pin Map

## ESP32-S3 SuperMini orientation used in this project

The diagram and tables below are drawn **from the component side with the USB-C connector at the bottom**, matching the physical orientation used in the build.

### ESP32-S3 SuperMini physical pin order — USB-C at bottom

| Left side, top → bottom | Right side, top → bottom |
|---|---|
| GPIO8 | GPIO7 |
| GPIO9 | GPIO6 |
| GPIO10 | GPIO5 |
| GPIO11 | GPIO4 |
| GPIO12 | GPIO3 |
| GPIO13 | GPIO2 |
| 3V3 | GPIO1 |
| GND | RX |
| 5V | TX |

## ILI9341 TFT → ESP32-S3 SuperMini

| ILI9341 pin | ESP32-S3 pin | Project function |
|---|---:|---|
| VCC | 3V3 | Display power |
| GND | GND | Ground |
| CS | GPIO8 | TFT chip select |
| RESET / RST | GPIO9 | TFT reset |
| DC / RS | GPIO10 | Data/command |
| SDI / MOSI | GPIO11 | SPI MOSI |
| SCK / CLK | GPIO12 | SPI clock |
| LED / BL | GPIO13 | Backlight control |
| SDO / MISO | **Not connected** | Not used by this project |

## Rotary encoder → ESP32-S3 SuperMini

For the raw 5-pin EC11-style encoder:

| Encoder pin | ESP32-S3 pin |
|---|---:|
| A | GPIO4 |
| COM (middle of 3-pin side) | GND |
| B | GPIO5 |
| SW1 | GND |
| SW2 | GPIO6 |

The firmware uses `INPUT_PULLUP`, so the encoder common and one switch terminal go to GND.

> **Important:** Follow the pin labels on your TFT module, not wire color. Some ILI9341 boards print `SDI` instead of `MOSI`, `SDO` instead of `MISO`, and `RS` instead of `DC`.
