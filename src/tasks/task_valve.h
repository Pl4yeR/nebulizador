#pragma once
#include <Arduino.h>

// Configures the solenoid pin and loads persisted config (or seeds defaults
// if none exists yet). Call once from setup().
void valveTaskBegin();

// Runs the proportional-control valve decision for the current sensor reading.
// hIndex/sensorValid should come from task_sensors. Non-blocking.
void valveTaskLoop(unsigned long now, float hIndex, bool sensorValid);

// Interval task_sensors should use between DHT reads — driven by the same
// proportional-control cadence the valve uses to decide when to act.
unsigned long valveTaskGetSensorIntervalMs();

bool valveTaskIsActive();

// Forces the valve closed immediately, bypassing the normal cycle. Used when
// handing control over to OTA mode.
void valveTaskForceClose();

// Getters — current control parameters, for task_mqtt telemetry/state sync.
float valveTaskGetMinHIndexThreshold();
float valveTaskGetMaxHIndexThreshold();
unsigned long valveTaskGetMaxFrequencyMs();
unsigned long valveTaskGetMinFrequencyMs();
unsigned long valveTaskGetValveActiveTimeMs();

// Setters — called only from task_mqtt's command callback. Each clamps the
// incoming value to a sane range (independent of whatever HA's own `number`
// entity min/max says, since the MQTT command topic is reachable by any
// client) and persists the result via config_store. Returns true once the
// stored state is valid — callers should re-read via the getter to see the
// actual (possibly clamped) resulting value, not assume the input was applied
// verbatim.
bool valveTaskSetMinHIndexThreshold(float celsius);
bool valveTaskSetMaxHIndexThreshold(float celsius);
bool valveTaskSetMaxFrequencyMs(unsigned long ms);
bool valveTaskSetMinFrequencyMs(unsigned long ms);
bool valveTaskSetValveActiveTimeMs(unsigned long ms);
