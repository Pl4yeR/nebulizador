/*
  NEBULIZADOR V3
*/

#include <Arduino.h>

#include "tasks/task_led.h"
#include "tasks/task_mqtt.h"
#include "tasks/task_ota.h"
#include "tasks/task_sensors.h"
#include "tasks/task_valve.h"

const unsigned int LOOP_DELAY_MS = 250; // Delay pacing the main loop

void setup() {
  Serial.begin(9600);
  Serial.println(F("[MAIN] Setup init."));

  ledTaskBegin();
  valveTaskBegin();
  sensorsTaskBegin();

  if (otaTaskCheckTrigger()) {
    otaTaskEnter(); // Never returns
    return;
  }

  mqttTaskBegin(); // Brings WiFi up for normal operation — MQTT needs it always-on now
  Serial.println(F("[MAIN] Nebulizador ready."));
}

void loop() {
  unsigned long now = millis();

  sensorsTaskLoop(now, valveTaskGetSensorIntervalMs());
  valveTaskLoop(now, sensorsGetHeatIndex(), sensorsReadIsValid());
  mqttTaskLoop(now);
  ledTaskLoop(now);
  otaTaskLoop();

  delay(LOOP_DELAY_MS);
}
