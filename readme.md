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

## Supported BLE adapters

| Adapter | Advertised name | Service | Write char | Notify char | Buy |
|---|---|---|---|---|---|
| Veepeak OBDCheck BLE | `VEEPEAK` | `FFF0` | `FFF2` | `FFF1` | [Amazon](https://www.amazon.com/dp/B073XKQQQW) |
| OBDSIM (desk simulator) | `OBDSIM` | `FFE0` | `FFE1` | `FFE1` | — |

## Request support for a new adapter

Adapters are added one at a time from real capture data, never from guesses. Open a GitHub issue with the **New adapter** template and include:

1. Exact brand and model, plus a purchase link from a major retailer (Amazon, Walmart, or manufacturer direct). No marketplace or unknown-seller links.
2. Your phone OS (iOS or Android).
3. Captures from Nordic's **nRF Connect for Mobile** (App Store / Play Store). Truck ON, adapter plugged in, nothing else connected to it — kill ABRP and any other OBD app first, since only one device can hold the link:
   - **Scan screen:** Complete Local Name plus the full Service UUID list from advertising data and scan response.
   - **Connected screen:** tap CONNECT, wait for services, then record every service UUID and, inside each, every characteristic UUID with its properties (READ, WRITE, WRITE WITHOUT RESPONSE, NOTIFY, INDICATE).
   - Flag which characteristic carries WRITE and which carries NOTIFY. That pair is the ELM327 doorway the firmware needs.
4. Screenshots of both screens attached to the issue. Copy UUIDs exactly, don't paraphrase.

## Quickstart

```powershell
pio run -e esp32-2432s028r -t upload --upload-port COM3
pio run -e obd-sim -t upload --upload-port COMx
pio device monitor -b 115200
pio test -e native
```

Type `status` in the monitor for theme, orientation, and link state. Type `orient portrait` to preview the parked layout.

## Firmware versions

The splash and boot log stamp every build from git, so the screen always tells you what it is:

* `v0.4.2` — clean build at a tag. Prod-ready, no suffix.
* `v0.4.3-dirty`, `v0.4.4-dirty`, ... — dev builds after tag `v0.4.2`. Every build auto-bumps patch and keeps the `-dirty` suffix, so each flash is unique and never mistaken for prod.
* Promotion: when `v0.4.3-dirty` passes smoke testing, commit exactly that source, tag `v0.4.3`, and clean-build. Prod `v0.4.3` is the identical firmware minus the suffix — the pair names one validated build. Picking a prod tag below already-flashed dev numbers prints a warning.

Release builds are gated: building with `RR_RELEASE=1` refuses to compile from a dirty tree, so a clean version string can never come from uncommitted source.

```powershell
pio run -e esp32-2432s028r -t upload --upload-port COM3            # dev/nightly
$env:RR_RELEASE = "1"; pio run -e esp32-2432s028r -t upload --upload-port COM3  # prod, fails if dirty
```

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
