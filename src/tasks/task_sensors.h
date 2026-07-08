#pragma once
#include <Arduino.h>

// Configures the DHT11 sensor. Call once from setup().
void sensorsTaskBegin();

// Reads the sensor if intervalMs has elapsed since the last read. Non-blocking.
void sensorsTaskLoop(unsigned long now, unsigned long intervalMs);

// Latest heat index in Celsius, or NAN if the last read failed / nothing read yet.
float sensorsGetHeatIndex();

// Latest raw readings, or NAN if the last read failed / nothing read yet.
float sensorsGetTemperature();
float sensorsGetHumidity();

// True if the latest reading is valid (humidity, temperature and heat index are all numbers).
bool sensorsReadIsValid();
