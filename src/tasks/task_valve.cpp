#include "task_valve.h"

#include "config_store.h"
#include "pins.h"
#include "task_led.h"

namespace {
// Proportional control: hIndex within [s_minHIndexThreshold, s_maxHIndexThreshold]
// maps to a next-check delay within [s_minFrequencyMs, s_maxFrequencyMs] via an
// ease-out curve — hotter means more frequent misting, with diminishing returns
// as it approaches the max. Defaults seed the persisted config on first boot;
// from then on the values below are loaded from config_store and can be
// changed at runtime via the setters (HA, through task_mqtt).
constexpr float DEFAULT_MIN_HINDEX_THRESHOLD = 29.8f;
constexpr float DEFAULT_MAX_HINDEX_THRESHOLD = 39.0f;
constexpr unsigned long DEFAULT_MAX_FREQUENCY_MS = 1800000; // 30 min
constexpr unsigned long DEFAULT_MIN_FREQUENCY_MS = 300000;  // 5 min
constexpr unsigned long DEFAULT_VALVE_ACTIVE_TIME_MS = 5000; // 5 s

float s_minHIndexThreshold = DEFAULT_MIN_HINDEX_THRESHOLD;
float s_maxHIndexThreshold = DEFAULT_MAX_HINDEX_THRESHOLD;
unsigned long s_maxFrequencyMs = DEFAULT_MAX_FREQUENCY_MS;
unsigned long s_minFrequencyMs = DEFAULT_MIN_FREQUENCY_MS;
unsigned long s_valveActiveTimeMs = DEFAULT_VALVE_ACTIVE_TIME_MS;

unsigned long s_cycleStartTime = 0;
unsigned long s_currentCycleDelayMs = 0; // 0 forces an immediate first check at boot
bool s_isValveActive = false;

void controlSolenoidValve(bool activate) {
  s_isValveActive = activate;
  digitalWrite(SOLENOID_PIN, activate ? HIGH : LOW);
  ledTaskSetValveActive(activate);
  Serial.println(activate ? F("[VALVE] Solenoid valve activated.") : F("[VALVE] Solenoid valve deactivated."));
}

// Applies the 5 in-memory values as a ValveConfig and persists them. Called
// after every setter and once at boot if no config file existed yet.
bool persistConfig() {
  ValveConfig cfg;
  cfg.minHIndexThresholdC = s_minHIndexThreshold;
  cfg.maxHIndexThresholdC = s_maxHIndexThreshold;
  cfg.maxFrequencyMs = s_maxFrequencyMs;
  cfg.minFrequencyMs = s_minFrequencyMs;
  cfg.valveActiveTimeMs = s_valveActiveTimeMs;
  return configStoreSaveValveConfig(cfg);
}
} // namespace

void valveTaskBegin() {
  ValveConfig cfg;
  if (configStoreLoadValveConfig(cfg)) {
    s_minHIndexThreshold = cfg.minHIndexThresholdC;
    s_maxHIndexThreshold = cfg.maxHIndexThresholdC;
    s_maxFrequencyMs = cfg.maxFrequencyMs;
    s_minFrequencyMs = cfg.minFrequencyMs;
    s_valveActiveTimeMs = cfg.valveActiveTimeMs;
    Serial.println(F("[VALVE] Loaded persisted config."));
  } else {
    Serial.println(F("[VALVE] No persisted config — seeding defaults."));
    persistConfig();
  }

  pinMode(SOLENOID_PIN, OUTPUT);
  digitalWrite(SOLENOID_PIN, LOW);
}

void valveTaskLoop(unsigned long now, float hIndex, bool sensorValid) {
  if (!sensorValid) {
    if (s_isValveActive) {
      Serial.println(F("[VALVE] Error reading sensors. Deactivating solenoid valve."));
      controlSolenoidValve(false);
    }
    if (now - s_cycleStartTime >= s_currentCycleDelayMs) {
      Serial.println(F("[VALVE] Error reading sensors. Skipping valve control."));
      s_cycleStartTime = now;
      s_currentCycleDelayMs = s_maxFrequencyMs;
    }

    return;
  }

  if (hIndex >= s_minHIndexThreshold) {
    if (!s_isValveActive && (now - s_cycleStartTime >= s_currentCycleDelayMs)) {
      // Ease-out mapping: decreases fast at the beginning, slower near the max.
      float factor = (hIndex - s_minHIndexThreshold) / (s_maxHIndexThreshold - s_minHIndexThreshold);
      float oneMinusFactor = 1.0f - factor;
      float easeOutFactor = 1.0f - (oneMinusFactor * oneMinusFactor * oneMinusFactor);

      s_currentCycleDelayMs =
          s_maxFrequencyMs - (unsigned long)(easeOutFactor * (s_maxFrequencyMs - s_minFrequencyMs));
      s_currentCycleDelayMs = constrain(s_currentCycleDelayMs, s_minFrequencyMs, s_maxFrequencyMs);

      Serial.print(F("[VALVE] hIndex ABOVE threshold. Activating valve for "));
      Serial.print(s_valveActiveTimeMs);
      Serial.println(F("ms."));
      Serial.print(F("[VALVE] Next check in "));
      Serial.println(String(s_currentCycleDelayMs) + F("ms."));

      controlSolenoidValve(true);
      s_cycleStartTime = now;
    }

    if (s_isValveActive && (now - s_cycleStartTime >= s_valveActiveTimeMs)) {
      controlSolenoidValve(false);
    }
  } else {
    if (s_isValveActive) {
      Serial.println(F("[VALVE] hIndex dropped BELOW threshold. Deactivating solenoid valve."));
      controlSolenoidValve(false);
    }
    if (now - s_cycleStartTime >= s_currentCycleDelayMs) {
      Serial.println(F("[VALVE] hIndex BELOW threshold. Valve remains closed."));
      s_cycleStartTime = now;
      s_currentCycleDelayMs = s_maxFrequencyMs;
      Serial.print(F("[VALVE] Next check in "));
      Serial.println(String(s_currentCycleDelayMs) + F("ms."));
    }
  }
}

unsigned long valveTaskGetSensorIntervalMs() { return s_currentCycleDelayMs; }

bool valveTaskIsActive() { return s_isValveActive; }

void valveTaskForceClose() { controlSolenoidValve(false); }

float valveTaskGetMinHIndexThreshold() { return s_minHIndexThreshold; }

float valveTaskGetMaxHIndexThreshold() { return s_maxHIndexThreshold; }

unsigned long valveTaskGetMaxFrequencyMs() { return s_maxFrequencyMs; }

unsigned long valveTaskGetMinFrequencyMs() { return s_minFrequencyMs; }

unsigned long valveTaskGetValveActiveTimeMs() { return s_valveActiveTimeMs; }

bool valveTaskSetMinHIndexThreshold(float celsius) {
  s_minHIndexThreshold = constrain(celsius, 15.0f, min(45.0f, s_maxHIndexThreshold - 0.1f));
  return persistConfig();
}

bool valveTaskSetMaxHIndexThreshold(float celsius) {
  s_maxHIndexThreshold = constrain(celsius, max(20.0f, s_minHIndexThreshold + 0.1f), 50.0f);
  return persistConfig();
}

bool valveTaskSetMaxFrequencyMs(unsigned long ms) {
  s_maxFrequencyMs = constrain(ms, max(60000UL, s_minFrequencyMs + 1000UL), 7200000UL);
  return persistConfig();
}

bool valveTaskSetMinFrequencyMs(unsigned long ms) {
  s_minFrequencyMs = constrain(ms, 10000UL, min(3600000UL, s_maxFrequencyMs - 1000UL));
  return persistConfig();
}

bool valveTaskSetValveActiveTimeMs(unsigned long ms) {
  s_valveActiveTimeMs = constrain(ms, 500UL, 60000UL);
  return persistConfig();
}
