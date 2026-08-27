# ESP-Brookesia Toolbox for ES3C28P

Developer toolbox firmware for the LCDWiki ES3C28P (ESP32-S3, ILI9341 320x240, FT6336G touch), rebuilt as native ESP-Brookesia Phone applications.

## Current migration state

- compact ESP-Brookesia launcher tuned for 320x240;
- working display and touch BSP for ES3C28P;
- **System** app with chip, heap, PSRAM, and uptime information;
- **I2C Scanner** app with non-blocking bus scan and known-device hints.

Planned ports from the original S3 Toolbox include UART Monitor, GPIO Tool, Logic Analyzer, Wi-Fi and BLE scanners, Audio Tool, Settings, storage/export services, and the web dashboard.

## Clone

```powershell
git clone --recursive <repository-url>
```

## Build

ESP-IDF 5.5 or newer is required.

```powershell
idf.py set-target esp32s3
idf.py build
idf.py -p COMx flash monitor
```

ESP-Brookesia is pinned as the `third_party/esp-brookesia` submodule on the `release/v0.6` branch.
