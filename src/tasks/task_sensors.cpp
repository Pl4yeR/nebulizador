#include "task_sensors.h"

#include <DHT.h>

#include "errors.h"
#include "pins.h"

namespace {
constexpr int DHT_TYPE = DHT11;
DHT s_dht(DHTPIN, DHT_TYPE);

float s_humidity = NAN;
float s_temperature = NAN;
float s_heatIndex = NAN;
unsigned long s_lastReadTime = 0;
} // namespace

void sensorsTaskBegin() {
  s_dht.begin();
  delay(50);
}

void sensorsTaskLoop(unsigned long now, unsigned long intervalMs) {
  if (now - s_lastReadTime < intervalMs)
    return;
  s_lastReadTime = now;

  Serial.println(F("[SENSORS] Reading DHT11..."));

  s_humidity = s_dht.readHumidity();
  s_temperature = s_dht.readTemperature();

  if (isnan(s_humidity) || isnan(s_temperature)) {
    Serial.println(F("[SENSORS] Failed to read from DHT sensor!"));
    s_heatIndex = NAN;
    setError(ErrorFlags::DHT);
    return;
  }
  clearError(ErrorFlags::DHT);

  s_heatIndex = s_dht.computeHeatIndex(s_temperature, s_humidity, false);

  Serial.print(F("[SENSORS] Humidity: "));
  Serial.print(s_humidity);
  Serial.println(F(" %"));

  Serial.print(F("[SENSORS] Temperature: "));
  Serial.print(s_temperature);
  Serial.println(F(" °C"));

  Serial.print(F("[SENSORS] Heat Index: "));
  Serial.print(s_heatIndex);
  Serial.println(F(" °C"));
}

float sensorsGetHeatIndex() { return s_heatIndex; }

bool sensorsReadIsValid() { return !isnan(s_humidity) && !isnan(s_temperature) && !isnan(s_heatIndex); }
