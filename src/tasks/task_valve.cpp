#include "task_valve.h"

#include "pins.h"
#include "task_led.h"

namespace {
// Proportional control: hIndex within [MIN_HINDEX_THRESHOLD, MAX_HINDEX_THRESHOLD]
// maps to a next-check delay within [MIN_FREQUENCY_MS, MAX_FREQUENCY_MS] via an
// ease-out curve — hotter means more frequent misting, with diminishing returns
// as it approaches the max.
constexpr float MIN_HINDEX_THRESHOLD = 29.8f;
constexpr float MAX_HINDEX_THRESHOLD = 39.0f;
constexpr unsigned long MAX_FREQUENCY_MS = 1800000; // 30 min
constexpr unsigned long MIN_FREQUENCY_MS = 300000;  // 5 min
constexpr unsigned long VALVE_ACTIVE_TIME_MS = 5000; // 5 s

unsigned long s_cycleStartTime = 0;
unsigned long s_currentCycleDelayMs = 0; // 0 forces an immediate first check at boot
bool s_isValveActive = false;

void controlSolenoidValve(bool activate) {
  s_isValveActive = activate;
  digitalWrite(SOLENOID_PIN, activate ? HIGH : LOW);
  ledTaskSetValveActive(activate);
  Serial.println(activate ? F("[VALVE] Solenoid valve activated.") : F("[VALVE] Solenoid valve deactivated."));
}
} // namespace

void valveTaskBegin() {
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
    }
    return;
  }

  if (hIndex >= MIN_HINDEX_THRESHOLD) {
    if (!s_isValveActive && (now - s_cycleStartTime >= s_currentCycleDelayMs)) {
      // Ease-out mapping: decreases fast at the beginning, slower near the max.
      float factor = (hIndex - MIN_HINDEX_THRESHOLD) / (MAX_HINDEX_THRESHOLD - MIN_HINDEX_THRESHOLD);
      float oneMinusFactor = 1.0f - factor;
      float easeOutFactor = 1.0f - (oneMinusFactor * oneMinusFactor * oneMinusFactor);

      s_currentCycleDelayMs =
          MAX_FREQUENCY_MS - (unsigned long)(easeOutFactor * (MAX_FREQUENCY_MS - MIN_FREQUENCY_MS));
      s_currentCycleDelayMs = constrain(s_currentCycleDelayMs, MIN_FREQUENCY_MS, MAX_FREQUENCY_MS);

      Serial.print(F("[VALVE] hIndex ABOVE threshold. Activating valve for "));
      Serial.print(VALVE_ACTIVE_TIME_MS);
      Serial.println(F("ms."));
      Serial.print(F("[VALVE] Next check in "));
      Serial.println(String(s_currentCycleDelayMs) + F("ms."));

      controlSolenoidValve(true);
      s_cycleStartTime = now;
    }

    if (s_isValveActive && (now - s_cycleStartTime >= VALVE_ACTIVE_TIME_MS)) {
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
      s_currentCycleDelayMs = MAX_FREQUENCY_MS;
      Serial.print(F("[VALVE] Next check in "));
      Serial.println(String(s_currentCycleDelayMs) + F("ms."));
    }
  }
}

unsigned long valveTaskGetSensorIntervalMs() { return s_currentCycleDelayMs; }

bool valveTaskIsActive() { return s_isValveActive; }

void valveTaskForceClose() { controlSolenoidValve(false); }
