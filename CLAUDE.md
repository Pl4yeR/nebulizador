# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Firmware for a water nebulizer/misting system, running on a Wemos D1 mini (ESP8266). It reads temperature/humidity from a DHT11 sensor, computes a heat index, and opens a solenoid valve (via a MOSFET trigger module) when it's hot enough — using non-blocking, cooperatively-scheduled "task" modules, not `delay()`-based sleeping between cycles.

There is a single firmware target: [platformio.ini](platformio.ini) + [src/nebulizadorv3.ino](src/nebulizadorv3.ino), built with PlatformIO for `env:d1_mini`. There is no legacy Arduino UNO / Arduino IDE target in this codebase.

## Build / flash

```
pio run                # compile
pio run -t upload      # compile + flash
pio device monitor     # serial monitor, 9600 baud
```

`lib_deps` in `platformio.ini` pulls the DHT sensor library, Adafruit Unified Sensor, ElegantOTA and ESP_MultiResetDetector from the PlatformIO registry — no manual library install needed. **`include/config.h` must exist locally before building** (WiFi/OTA credentials, gitignored — see `include/.gitignore`) since `task_ota.cpp` includes it unconditionally; the build fails without it. There is no linter or test suite; `pio run` (compile-only, no board attached) is the way to check correctness after edits. The firmware prints to Serial at 9600 baud, prefixed by task (`[MAIN]`, `[SENSORS]`, `[VALVE]`, `[OTA]`) — the primary way to observe/debug behavior since there's no test harness.

## Pin configuration

All GPIO assignments live in [include/pins.h](include/pins.h) as `#ifndef`-guarded macros (`DHTPIN`, `SOLENOID_PIN`), so they're overridable from `platformio.ini` via `build_flags` without touching code:

```ini
build_flags =
    -D DHTPIN=D6
    -D SOLENOID_PIN=D2
```

Defaults: `DHTPIN=D5`, `SOLENOID_PIN=D1`. The status LED is *not* in `pins.h` — it's always `LED_BUILTIN` (GPIO2 / `D4`, active-low on the D1 mini), since that's fixed by the board rather than a wiring choice.

**Deliberately removed** (do not reintroduce without being asked): the luminosity/LDR sensor, the physical manual-override button, and separate activity/error status LEDs. The only status indicator is the board's built-in LED. There is no dedicated visual error indicator; sensor errors are Serial-only.

## Task architecture — and why it isn't FreeRTOS

This firmware's structure is modeled on a sibling project, **Neverina** (an ESP32 fridge controller), which uses genuine FreeRTOS tasks (`xTaskCreatePinnedToCore`, queues, event groups) — real preemptive threads pinned to specific cores.

**The Wemos D1 mini here is an ESP8266, not an ESP32.** The `platform = espressif8266` Arduino core has no FreeRTOS: no `freertos/*.h` headers ship with the framework at all (verified — there's nothing to include). So "tasks" in this codebase are **cooperative, single-threaded modules**, not real threads:

- Each task module exposes a `xxxTaskLoop(now, ...)` function that does a small unit of work and returns immediately (never blocks, except deliberately inside `otaTaskEnter()` — see below).
- `src/nebulizadorv3.ino`'s `loop()` calls each task's `Loop()` function once per 250ms tick (`LOOP_DELAY_MS`), in a fixed order, and that's the entire scheduler. There's no preemption, no priorities, no separate stacks.
- Modules talk to each other through small, explicit getter/setter functions (e.g. `sensorsGetHeatIndex()`, `ledTaskSetMode()`) — not shared queues, since there's no concurrency to guard against.

If real concurrency (e.g. genuinely parallel WiFi handling while doing time-sensitive GPIO work) is ever needed, the only way to get actual FreeRTOS on this hardware is to swap to an ESP32-based board (e.g. Wemos D1 mini32, which is what Neverina runs on) — that decision was explicitly deferred; see the "hardware target" discussion in project history if you need to revisit it.

## Source structure

```
include/
  pins.h              — DHTPIN / SOLENOID_PIN, #ifndef-guarded, overridable via build_flags
  config.h            — WiFi/OTA credentials (gitignored, not committed — create locally)
src/
  nebulizadorv3.ino    — setup()/loop() orchestrator only; wires task modules together
  tasks/
    task_sensors.h/.cpp — owns the DHT11, exposes sensorsGetHeatIndex() / sensorsReadIsValid()
    task_valve.h/.cpp   — proportional-control valve decision + SOLENOID_PIN
    task_led.h/.cpp     — LED_BUILTIN state machine (boot blink / valve mirror / OTA fast-blink)
    task_ota.h/.cpp     — triple-reset detection + ElegantOTA over WiFi
```

### task_sensors
Owns the `DHT` object and the latest humidity/temperature/heat-index reading. `sensorsTaskLoop(now, intervalMs)` only re-reads the DHT11 once `intervalMs` has elapsed since the last read — the caller (currently `task_valve`, via `valveTaskGetSensorIntervalMs()`) decides that cadence, so the sensor read frequency tracks the same 5min–30min proportional-control interval the valve uses. `hIndex = NAN` on read failure.

### task_valve
The core decision function, `valveTaskLoop(now, hIndex, sensorValid)`:
- `!sensorValid` → sensor-error path: closes the valve if it was open, logs, and returns (retries next tick, cadence unchanged — see caveat below).
- `hIndex >= MIN_HINDEX_THRESHOLD` (29.8°C) → opens the valve for `VALVE_ACTIVE_TIME_MS` (5s), then computes the next check interval via an ease-out curve mapping `hIndex` within `[29.8, 39]`°C to a delay within `[5min, 30min]` — hotter means more frequent misting.
- `hIndex < MIN_HINDEX_THRESHOLD` → valve stays closed, next check pinned to 30min.

`controlSolenoidValve()` drives `SOLENOID_PIN` (active-high: `HIGH` = valve open) and calls `ledTaskSetMode()` to mirror state on the LED.

**Pre-existing quirk, carried forward from the pre-task-refactor code:** because `task_sensors`' read cadence is driven by `task_valve`'s own interval, a sensor fault during a "calm" (cold) 30-minute interval won't get a fresh retry read for up to 30 minutes — `valveTaskLoop`'s error branch re-checks the same stale `NAN` every 250ms but doesn't force an earlier re-read. This was true before the refactor too; fixing it is a behavior change, not an architecture one.

### task_led
`LedMode` enum (`Idle` / `ValveActive` / `Ota`) set by whichever task owns that concern (`task_valve` sets `Idle`/`ValveActive`, `task_ota` sets `Ota`). `ledTaskLoop(now)` only does work in `Ota` mode (100ms fast blink); the other two modes are static (solid on/off), set once in `ledTaskSetMode()`.

### task_ota
Modeled on Neverina's OTA mode, adapted for ESP8266 (`ESP8266WiFi`/`ESP8266WebServer` instead of `WiFi`/`WebServer`, `ESP.getChipId()` instead of `ESP.getEfuseMac()`). No BLE trigger exists in this project (there's no BLE at all), so **the only OTA trigger is a triple power-cycle within 10s**, detected via `ESP_MultiResetDetector` (EEPROM-backed):
- `otaTaskCheckTrigger()` — call once, early in `setup()`. Returns `true` if this boot is the 3rd rapid reset.
- If triggered, `otaTaskEnter()` is called and **never returns**: it force-closes the valve, connects to `WIFI_SSID`/`WIFI_PASSWORD` (15s timeout, falls back to AP mode `Nebulizador-OTA-<chipid>` + `OTA_AP_PASSWORD`), serves ElegantOTA at `/update`, and blocks forever in its own loop (calling `ledTaskLoop()` itself, since the main `loop()` never regains control).
- If not triggered, `otaTaskDisableWifi()` turns WiFi off for normal operation (nebulizer control doesn't need the radio), and `otaTaskLoop(now)` is called every normal-mode tick just to clear the reset-detection window after it elapses.

WiFi is **only** brought up in OTA mode today. Using the D1's WiFi for anything else (e.g. runtime configuration, status reporting) is future work — the task-based structure exists to make that easier to add without threading everything through `loop()`.

## Git history note

This repository has diverged branches with substantially different firmware architectures (an older Arduino UNO "caso"-table design vs. this proportional-control D1 mini rewrite). If you're comparing against old commits or other branches, don't assume their logic, pin maps, or file layout apply here — this file describes the code as it exists on the current branch only.
