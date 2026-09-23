# RangeReality

[![Platform](https://img.shields.io/badge/ESP32-Arduino-000000?style=flat-square&logo=espressif&logoColor=white)](platformio.ini)
[![Repo Size](https://img.shields.io/github/repo-size/upioneer/RangeReality?style=flat-square&color=38bdf8)](https://github.com/upioneer/RangeReality)

Real world range telemetry for the Ford F-150 Lightning, on a dash mounted ESP32 display. It replaces the factory guess-o-meter with live pack math, pace based range, and driving advice that accounts for tires, wind, hills, and load.

## Hardware

* ESP32-2432S028R CYD (2.8 inch 320x240, ST7789) running the car UI
* BLE OBD2 adapter (Veepeak profile first, registry is extensible)
* Spare ESP32 running the desk simulator for development without the truck
* Optional later: MPU6050 accelerometer, GPS module

## What works now

* LVGL interface with splash, trip strip, hero Pace Range, advisory marquee, and a two color gradient power gauge with session peaks
* Park detection flips the gauge into a full bar charge meter (0 to 200 kW)
* BLE central link with adapter profiles, ELM327 init, and live pack volts and amps
* Standard theme in landscape, portrait parked until acceptance
* BOOT button input: single press reserved, double press cycles themes, long press resets trip
* Host tested meter math via `pio test -e native`

## Quickstart

```powershell
pio run -e esp32-2432s028r -t upload --upload-port COM3
pio run -e obd-sim -t upload --upload-port COMx
pio device monitor -b 115200
pio test -e native
```

Type `status` in the monitor for theme, orientation, and link state. Type `orient portrait` to preview the parked layout.

## Project structure

```
.
├── src/                  # Car UI firmware (main, themes, state, meter, button)
├── src/obd/              # ELM327 protocol and BLE central link
├── src/sim_main.cpp       # Desk simulator firmware (obd-sim env)
├── include/              # LVGL config (unique name, see readme in file)
├── test/                 # Host Unity tests for meter math
├── project_details/      # design.md, theme mocks, agent rules
└── TODO.md               # Polish list for after truck validation
```

## Docs

* `project_details/design.md`: vehicle, hardware, and UI architecture
* `project_details/assets/mock/`: kitt, minimalist, standard, steampunk theme targets
* `TODO.md`: deferred polish items

## License

Proprietary, see [LICENSE.md](./LICENSE.md).
