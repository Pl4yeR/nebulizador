# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Firmware for a water nebulizer/misting system, running on a Wemos D1 mini (ESP8266). It reads temperature/humidity from a DHT11 sensor, computes a heat index, and opens a solenoid valve (via a MOSFET trigger module) when it's hot enough — using a non-blocking main loop with proportional control, not `delay()`-based sleeping between cycles.

There is a single firmware target: [platformio.ini](platformio.ini) + [src/nebulizadorv3.ino](src/nebulizadorv3.ino), built with PlatformIO for `env:d1_mini`. There is no legacy Arduino UNO / Arduino IDE target in this codebase.

## Build / flash

```
pio run                # compile
pio run -t upload      # compile + flash
pio device monitor     # serial monitor, 9600 baud
```

`lib_deps` in `platformio.ini` pulls the DHT sensor library + Adafruit Unified Sensor from the PlatformIO registry — no manual library install needed. There is no linter or test suite; `pio run` (compile-only, no board attached) is the way to check correctness after edits. The firmware prints sensor readings and valve timing decisions to Serial at 9600 baud — the primary way to observe/debug behavior since there's no test harness.

## Pin configuration

All GPIO assignments live in [include/pins.h](include/pins.h) as `#ifndef`-guarded macros (`DHTPIN`, `SOLENOID_PIN`), so they're overridable from `platformio.ini` via `build_flags` without touching code:

```ini
build_flags =
    -D DHTPIN=D6
    -D SOLENOID_PIN=D2
```

Defaults: `DHTPIN=D5`, `SOLENOID_PIN=D1`. The status LED is *not* in `pins.h` — it's always `LED_BUILTIN` (GPIO2 / `D4`, active-low on the D1 mini), since that's fixed by the board rather than a wiring choice.

**Deliberately removed** (do not reintroduce without being asked): the luminosity/LDR sensor, the physical manual-override button, and the separate activity/error status LEDs. The only status indicator is the board's built-in LED — it mirrors valve state (on while misting) and blinks 3 times at boot. There is no dedicated visual error indicator; sensor errors are Serial-only.

## Control flow

Everything runs from a single non-blocking `loop()` that reads `millis()` once and passes it down — no `delay()` between decisions, only a small fixed `LOOP_DELAY_MS` (250ms) pacing the loop itself:

1. **`readSensorsLoop()`** — reads the DHT11 (`readDHTSensor`) once every `currentCycleDelayMs`, not every loop tick.
2. **`readDHTSensor()`** — reads humidity/temperature, computes `hIndex` (heat index, Celsius) via the DHT library. Sets `hIndex = NAN` on read failure.
3. **`manageValveLoop()`** — the core decision function:
   - NaN humidity/temperature/hIndex → sensor-error path: closes the valve if it was open, logs, and returns (retries next loop tick, no long backoff).
   - `hIndex >= MIN_HINDEX_THRESHOLD` (29.8°C) → opens the valve for a fixed `VALVE_ACTIVE_TIME_MS` (5s), then computes the next check interval via **proportional control**: an ease-out curve maps `hIndex` within `[MIN_HINDEX_THRESHOLD, MAX_HINDEX_THRESHOLD]` (29.8–39°C) to a delay within `[MIN_FREQUENCY_MS, MAX_FREQUENCY_MS]` (5min–30min) — hotter means more frequent misting, with diminishing returns as it approaches the max.
   - `hIndex < MIN_HINDEX_THRESHOLD` → valve stays closed, next check pinned to `MAX_FREQUENCY_MS` (30min).
4. **`controlSolenoidValve(activate)`** — drives `SOLENOID_PIN` (active-high: `HIGH` = valve open) and mirrors the state on `LED_BUILTIN` (active-low, so `LOW` = LED on while the valve is open).

## Git history note

This repository has diverged branches with substantially different firmware architectures (an older Arduino UNO "caso"-table design vs. this proportional-control D1 mini rewrite). If you're comparing against old commits or other branches, don't assume their logic, pin maps, or file layout apply here — this file describes the code as it exists on the current branch only.
