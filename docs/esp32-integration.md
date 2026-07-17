# ESP32 Integration Guide

## Required libraries

Install these via the Arduino Library Manager:

- `TFT_eSPI` by Bodmer
- `WiFiManager` by tablatown
- `ArduinoJson` by Benoit Blanchon
- `ESP32Servo` by Kevin Harrington

The ESP32 core for Arduino IDE should be version 2.0.0 or newer.

## TFT_eSPI setup

`TFT_eSPI` requires a setup file with your pin mappings. Copy the contents below into your `TFT_eSPI/User_Setup.h` file (or create a custom setup file and select it in `User_Setup_Select.h`).

```cpp
#define USER_SETUP_INFO "Lovebox ESP32-S3"

#define ESP32_PARALLEL_0 // placeholder, replaced by SPI below
#define ILI9341_DRIVER

#define TFT_WIDTH  240
#define TFT_HEIGHT 320

#define TFT_MISO 13
#define TFT_MOSI 11
#define TFT_SCLK 12
#define TFT_CS   10
#define TFT_DC    9
#define TFT_RST  14

#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF
#define SMOOTH_FONT

#define SPI_FREQUENCY  40000000
#define SPI_READ_FREQUENCY 20000000
```

Set `TFT_MISO` even if you do not read from the display; it keeps the SPI bus definition correct.

## Wiring

| Function | ESP32-S3 pin |
|---|---|
| TFT CS | GPIO 10 |
| TFT DC | GPIO 9 |
| TFT RST | GPIO 14 |
| TFT MOSI | GPIO 11 |
| TFT SCK | GPIO 12 |
| TFT MISO | GPIO 13 |
| Servo signal | GPIO 15 |
| TFT/Servo GND | GND |
| TFT VCC | 3V3 |
| Servo power | 5V (external regulated supply recommended for final build) |

## Configure the firmware

Open `firmware/lovebox_esp32/lovebox_esp32.ino` and edit these three values near the top:

```cpp
const char* DEVICE_ID = "lovebox-001";
const char* DEVICE_KEY = "your-device-secret-from-netlify-env";
const char* API_HOST = "https://your-site-name.netlify.app";
```

`DEVICE_KEY` must match the `DEVICE_KEY_LOVEBOX_001` value set in the Netlify environment variables.

## Flashing

1. Connect the ESP32-S3 to your computer via USB-C.
2. Select the correct board in Arduino IDE: `ESP32S3 Dev Module`.
3. Select the correct port and upload.
4. Open the Serial Monitor at 115200 baud to watch connection status.

## First-time Wi-Fi setup

On first boot, the device creates an access point named `LOVEBOX-SETUP`. Connect a phone to it and open the captive portal (usually appears automatically, or navigate to `192.168.4.1`). Enter the home Wi-Fi credentials. The ESP32 saves them in flash and reconnects automatically after power cycles.

## Security note

The firmware currently uses `client.setInsecure()` for HTTPS to avoid certificate management issues in the MVP. This is acceptable for a personal gift on a trusted home network, but for production use you should replace it with proper root certificate validation. Add a `const char* rootCACertificate` and call `client.setCACert(rootCACertificate)`.

## Power note

The SG90 servo can cause voltage dips when powered from USB. For the final build, power the servo from a regulated external 5V supply with a common ground to the ESP32, and add a 100 µF bulk capacitor near the servo.
