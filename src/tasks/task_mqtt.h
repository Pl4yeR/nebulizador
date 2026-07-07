#pragma once
#include <Arduino.h>

// Brings up WiFi (STA) and configures the MQTT client. Call once from setup(),
// normal-mode path only (not before/instead of OTA).
void mqttTaskBegin();

// Drives the connection/reconnect state machine, republishes telemetry when
// it changes, and services the MQTT client. Call every normal-mode loop tick.
void mqttTaskLoop(unsigned long now);

// Publishes "offline" (retained) and cleanly disconnects, if connected. Safe
// to call even if mqttTaskBegin() was never called this boot (no-op). Used by
// task_ota before entering OTA mode, since OTA overrides everything else.
void mqttTaskDisconnect();
