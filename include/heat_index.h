#pragma once

// NOAA/NWS heat index (Rothfusz regression + Steadman low-humidity/high-humidity
// refinements), Celsius in, Celsius out. Matches the formula used by the
// Adafruit DHT sensor library's DHT::computeHeatIndex(), so both sensor
// backends (task_sensors_dht.cpp / task_sensors_sht30.cpp) compute an
// identical heat index for the same temperature/humidity reading.
float computeHeatIndexC(float temperatureC, float humidityPercent);
