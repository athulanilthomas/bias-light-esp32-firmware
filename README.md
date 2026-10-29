# TV Bias Light: ESP32 firmware

ESP-IDF (v5.x, C) firmware that drives a WS2812B strip behind a TV, controlled from a phone over Wi-Fi, with state kept across power cuts.

| Phase | Status |
|-------|--------|
| 1. Core output (effects, gamma, brightness cap, 60 FPS render task) | Implemented |
| 2. Network control (Wi-Fi STA, REST API, web UI, `tvlight.local`) | Implemented |
| 3. Persistence (debounced NVS save, restore on boot) | Implemented |
| 4. HomeKit | Not started |
| 5. Extras (SNTP schedule, UDP ambilight) | Not started |

Developed and compile-tested against ESP-IDF v5.4. Not yet exercised on real hardware.

## Build, flash, monitor

```sh
. $IDF_PATH/export.sh
idf.py set-target esp32          # once
idf.py menuconfig                # "Bias Light: ..." menus: Wi-Fi SSID/password, LED count/GPIO, brightness cap
idf.py build flash monitor
```

Managed components (`led_strip`, `mdns`) are fetched automatically on the first build; versions are pinned by `dependencies.lock`.

Wi-Fi credentials live only in your local, git-ignored `sdkconfig`. Leave the SSID empty and the strip still runs (no network).

## Hardware and wiring

- ESP32-WROOM-32 dev board, WS2812B 5 V strip (about 60-90 LEDs).
- Data: `GPIO18` (`CONFIG_LED_GPIO`) to the strip's DIN.
- Power the strip from its own 5 V supply. **Connect that supply's GND to the ESP32 GND.**
- The ESP32 outputs 3.3 V; WS2812B expects ~0.7 x Vdd = 3.5 V. It often works directly, but a 74AHCT125/74HCT245 level shifter is more reliable.
- Add a ~330 ohm resistor in series with the data line close to the first LED, and a 1000 uF capacitor across the strip's 5 V/GND at the supply.
- Power budget: a WS2812B draws up to ~60 mA at full white, i.e. 3.6 A for 60 LEDs. With the default 40% cap the strip stays under about 1.5 A, so a 5 V / 3 A supply is plenty.
- For more than ~60 LEDs, inject 5 V at both ends of the strip.

## Configuration (menuconfig)

| Option | Default | Meaning |
|--------|---------|---------|
| `CONFIG_LED_COUNT` | 60 | Number of LEDs |
| `CONFIG_LED_GPIO` | 18 | Data pin |
| `CONFIG_MAX_BRIGHTNESS_PCT` | 40 | Hard cap on strip output |
| `CONFIG_RENDER_FPS` | 60 | Render loop rate |
| `CONFIG_DEFAULT_BRIGHTNESS_PCT` | 50 | Brightness when nothing is saved |
| `CONFIG_WIFI_SSID` / `CONFIG_WIFI_PASSWORD` | empty | Station credentials |
| `CONFIG_MDNS_HOSTNAME` | `tvlight` | `http://tvlight.local/` |
| `CONFIG_STORAGE_SAVE_DEBOUNCE_MS` | 5000 | Minimum spacing of NVS writes |
| `CONFIG_HEAP_LOG_INTERVAL_S` | 60 | Free-heap log period |

## REST API

`GET /` serves the phone UI (embedded in firmware). `GET /api/state` returns:

```json
{"on":true,"mode":"warm_white","color":[255,147,41],"brightness":50}
```

`POST /api/state` accepts any subset of those fields and returns the new state:

```sh
curl -X POST http://tvlight.local/api/state -d '{"mode":"solid","color":[0,80,255],"brightness":30}'
```

- `mode`: `solid`, `warm_white`, `breathe`, `rainbow`. `color` is used by `solid` and `breathe` only.
- `brightness` is 0-100 and is scaled **onto** 0..`CONFIG_MAX_BRIGHTNESS_PCT`, so 100 means "the cap", never full power.
- Validation: wrong types or unknown modes get HTTP 400; numbers out of range are clamped; bodies over 256 bytes get 413.

## Design notes

- **Render loop**: one FreeRTOS task pinned to core 1 (Wi-Fi runs on core 0) at ~60 FPS. Per-pixel work is integer only; the gamma (2.2) and breathe tables are built once at start-up. Brightness changes and on/off fade over ~100 ms instead of popping. Unchanged frames are not re-sent (except a 1 s keep-alive), which reduces flicker risk from Wi-Fi interrupts. The only wait is the RMT transfer inside `led_strip_refresh` (~2 ms for 60 LEDs).
- **Gamma and cap order**: colour -> gamma -> multiply by (brightness x cap). The cap therefore limits linear light output and cannot be exceeded by any API input.
- **Persistence**: `on`, `mode`, `color`, `brightness` are stored as one versioned NVS blob. Changes are coalesced by a writer task and written at most once per `CONFIG_STORAGE_SAVE_DEBOUNCE_MS`; identical state is never rewritten. A change made within the last few seconds before a power cut can be lost.
- **Watchdog**: the task watchdog is enabled with panic-on-timeout (5 s) and the render task is subscribed; any crash or hang reboots into the last saved state.
- **Heap**: free/min-free/largest-block heap is logged every `CONFIG_HEAP_LOG_INTERVAL_S` seconds (tag `main`) for soak testing. The HTTP path frees all per-request allocations.
- **Partition table**: `partitions.csv` has two 1.75 MB OTA slots for a future OTA feature (4 MB flash assumed).

## Assumptions and deviations

- GPIO 18, 60 LEDs, 40% cap, 4 MB flash were assumed per the Kconfig defaults.
- Brightness API unit is 0-100 within the cap range (see above).
- `espressif/mdns` was added as a managed component because ESP-IDF 5.x no longer bundles it; it is required for `tvlight.local`.
- Stretch goals not implemented yet: SoftAP captive-portal fallback (the strip keeps retrying the station connection with back-off), OTA update endpoint.

## Project layout

```
main/                 app_main, heap monitor
components/effects/   strip output, effects, gamma, state, render task
components/wifi_mgr/  Wi-Fi station + mDNS
components/http_api/  REST API + embedded index.html
components/storage/   debounced NVS persistence
```
