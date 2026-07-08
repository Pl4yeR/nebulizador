#pragma once
#include <Arduino.h>

// Checks for a triple power-cycle within the reset window. Call once, early in
// setup(), before other GPIO/sensor init. Returns true if OTA mode should be
// entered immediately via otaTaskEnter().
bool otaTaskCheckTrigger();

// Lets the reset detector clear its window once it has elapsed. Call on every
// normal (non-OTA) loop tick.
void otaTaskLoop();

// Connects to WiFi (reusing it if task_mqtt already brought it up) and serves
// ElegantOTA at /update. Never returns — this is a dead end only reached when
// otaTaskCheckTrigger() returns true.
void otaTaskEnter();
