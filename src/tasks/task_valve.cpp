#include "task_valve.h"

#include "config_store.h"
#include "pins.h"
#include "task_led.h"
#include "task_time.h"

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
// 0/0 means "no schedule" — scheduleAllowsNow() treats start == end as always-on.
constexpr uint16_t DEFAULT_START_MINUTE_OF_DAY = 0;
constexpr uint16_t DEFAULT_END_MINUTE_OF_DAY = 0;

float s_minHIndexThreshold = DEFAULT_MIN_HINDEX_THRESHOLD;
float s_maxHIndexThreshold = DEFAULT_MAX_HINDEX_THRESHOLD;
unsigned long s_maxFrequencyMs = DEFAULT_MAX_FREQUENCY_MS;
unsigned long s_minFrequencyMs = DEFAULT_MIN_FREQUENCY_MS;
unsigned long s_valveActiveTimeMs = DEFAULT_VALVE_ACTIVE_TIME_MS;
uint16_t s_startMinuteOfDay = DEFAULT_START_MINUTE_OF_DAY;
uint16_t s_endMinuteOfDay = DEFAULT_END_MINUTE_OF_DAY;

unsigned long s_cycleStartTime = 0;
unsigned long s_currentCycleDelayMs = 0; // 0 forces an immediate first check at boot
bool s_isValveActive = false;

// Manual override (task_mqtt's "switch"): not persisted — a reboot always
// comes up with no manual cycle in progress, regardless of what was
// happening before the restart.
bool s_manualActive = false;
unsigned long s_manualStartTime = 0;

// Edge-triggered logging only — scheduleAllowsNow() itself has no side effects.
bool s_scheduleWasBlocking = false;

void controlSolenoidValve(bool activate) {
  s_isValveActive = activate;
  digitalWrite(SOLENOID_PIN, activate ? HIGH : LOW);
  ledTaskSetValveActive(activate);
  Serial.println(activate ? F("[VALVE] Solenoid valve activated.") : F("[VALVE] Solenoid valve deactivated."));
}

// Applies the 7 in-memory values as a ValveConfig and persists them. Called
// after every setter and once at boot if no config file existed yet.
bool persistConfig() {
  ValveConfig cfg;
  cfg.minHIndexThresholdC = s_minHIndexThreshold;
  cfg.maxHIndexThresholdC = s_maxHIndexThreshold;
  cfg.maxFrequencyMs = s_maxFrequencyMs;
  cfg.minFrequencyMs = s_minFrequencyMs;
  cfg.valveActiveTimeMs = s_valveActiveTimeMs;
  cfg.startMinuteOfDay = s_startMinuteOfDay;
  cfg.endMinuteOfDay = s_endMinuteOfDay;
  return configStoreSaveValveConfig(cfg);
}

// start == end disables the schedule (24h operation). Without a synced clock
// there's no way to evaluate the window, so we run unrestricted rather than
// blocking misting indefinitely — same "degrade to always-on" rule as a
// missing schedule. A window that crosses midnight (start > end) is
// interpreted as "from start until end the next day".
bool scheduleAllowsNow() {
  if (s_startMinuteOfDay == s_endMinuteOfDay || !timeTaskIsSynced())
    return true;

  int m = timeTaskMinutesOfDay();
  if (s_startMinuteOfDay <= s_endMinuteOfDay)
    return m >= s_startMinuteOfDay && m <= s_endMinuteOfDay;
  return m >= s_startMinuteOfDay || m <= s_endMinuteOfDay;
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
    s_startMinuteOfDay = cfg.startMinuteOfDay;
    s_endMinuteOfDay = cfg.endMinuteOfDay;
    Serial.println(F("[VALVE] Loaded persisted config."));
  } else {
    Serial.println(F("[VALVE] No persisted config — seeding defaults."));
    persistConfig();
  }

  pinMode(SOLENOID_PIN, OUTPUT);
  digitalWrite(SOLENOID_PIN, LOW);
}

void valveTaskLoop(unsigned long now, float hIndex, bool sensorValid) {
  // Manual override has priority over everything else, including an invalid
  // sensor reading — it's an explicit user action, not a control decision.
  // Automatic cycle timers are left untouched (not evaluated) while it runs.
  if (s_manualActive) {
    if (now - s_manualStartTime >= s_valveActiveTimeMs) {
      Serial.println(F("[VALVE] Manual cycle finished."));
      controlSolenoidValve(false);
      s_manualActive = false;
    }
    return;
  }

  if (!scheduleAllowsNow()) {
    if (s_isValveActive) {
      Serial.println(F("[VALVE] Outside allowed schedule. Deactivating solenoid valve."));
      controlSolenoidValve(false);
    }
    if (!s_scheduleWasBlocking) {
      Serial.println(F("[VALVE] Outside schedule window — automatic control suspended."));
      s_scheduleWasBlocking = true;
    }
    // Cycle timers stay frozen while blocked, so the moment the window opens
    // again the current cycle reads as already-elapsed and re-evaluates immediately.
    return;
  }
  if (s_scheduleWasBlocking) {
    Serial.println(F("[VALVE] Back within schedule window — automatic control resumed."));
    s_scheduleWasBlocking = false;
  }

  if (!sensorValid) {
    if (s_isValveActive) {
      Serial.println(F("[VALVE] Error reading sensors. Deactivating solenoid valve."));
      controlSolenoidValve(false);
    }
    if (now - s_cycleStartTime >= s_currentCycleDelayMs)
    {
      Serial.println(F("[VALVE] Error reading sensors. Skipping valve control."));
      s_currentCycleDelayMs = 5000; // Retry in 5s
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

void valveTaskForceClose() {
  s_manualActive = false;
  controlSolenoidValve(false);
}

unsigned long valveTaskGetCycleStartMs() { return s_cycleStartTime; }

bool valveTaskManualIsActive() { return s_manualActive; }

void valveTaskSetManual(bool on) {
  if (on) {
    s_manualActive = true;
    s_manualStartTime = millis();
    Serial.println(F("[VALVE] Manual cycle started."));
    controlSolenoidValve(true);
  } else if (s_manualActive) {
    s_manualActive = false;
    Serial.println(F("[VALVE] Manual cycle cancelled."));
    controlSolenoidValve(false);
  }
}

float valveTaskGetMinHIndexThreshold() { return s_minHIndexThreshold; }

float valveTaskGetMaxHIndexThreshold() { return s_maxHIndexThreshold; }

unsigned long valveTaskGetMaxFrequencyMs() { return s_maxFrequencyMs; }

unsigned long valveTaskGetMinFrequencyMs() { return s_minFrequencyMs; }

unsigned long valveTaskGetValveActiveTimeMs() { return s_valveActiveTimeMs; }

uint16_t valveTaskGetStartMinuteOfDay() { return s_startMinuteOfDay; }

uint16_t valveTaskGetEndMinuteOfDay() { return s_endMinuteOfDay; }

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

bool valveTaskSetStartMinuteOfDay(uint16_t minuteOfDay) {
  s_startMinuteOfDay = constrain(minuteOfDay, 0, 1439);
  return persistConfig();
}

bool valveTaskSetEndMinuteOfDay(uint16_t minuteOfDay) {
  s_endMinuteOfDay = constrain(minuteOfDay, 0, 1439);
  return persistConfig();
}
