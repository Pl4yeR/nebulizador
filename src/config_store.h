#pragma once
#include <Arduino.h>

// Persisted, HA-configurable valve control parameters. Internal units only
// (ms / °C) — task_mqtt handles conversion to/from human-friendly HA units.
struct ValveConfig {
  float minHIndexThresholdC;
  float maxHIndexThresholdC;
  uint32_t maxFrequencyMs;
  uint32_t minFrequencyMs;
  uint32_t valveActiveTimeMs;
  // Minutes since midnight (0-1439). start == end means the schedule is
  // disabled (valve control runs 24h) — see task_valve.cpp's scheduleAllowsNow().
  uint16_t startMinuteOfDay;
  uint16_t endMinuteOfDay;
};

// Mounts LittleFS lazily on first use. Returns false if no valid config file
// exists yet (missing, corrupt, or an old schema version) — caller should
// fall back to hardcoded defaults and save them via configStoreSaveValveConfig().
bool configStoreLoadValveConfig(ValveConfig &out);

// Writes to a temp file then renames over the real one, so a power loss
// mid-write can't leave a corrupt config file behind.
bool configStoreSaveValveConfig(const ValveConfig &cfg);
