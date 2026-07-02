#pragma once
#include <Arduino.h>

// Configures the solenoid pin. Call once from setup().
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
