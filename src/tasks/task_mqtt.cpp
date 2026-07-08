#include "task_mqtt.h"

#include <ESP8266WiFi.h>
#include <PubSubClient.h>

#include "config.h"
#include "errors.h"
#include "task_sensors.h"
#include "task_time.h"
#include "task_valve.h"

namespace {
constexpr const char *DEVICE_ID = "nebulizador";

constexpr const char *TOPIC_DISCOVERY = "homeassistant/device/nebulizador/config";
constexpr const char *TOPIC_STATE = "homeassistant/nebulizador/state";
constexpr const char *TOPIC_AVAILABILITY = "homeassistant/nebulizador/availability";

constexpr const char *TOPIC_CMD_MIN_HINDEX = "homeassistant/nebulizador/number/min_hindex/set";
constexpr const char *TOPIC_CMD_MAX_HINDEX = "homeassistant/nebulizador/number/max_hindex/set";
constexpr const char *TOPIC_CMD_MAX_FREQUENCY = "homeassistant/nebulizador/number/max_frequency_min/set";
constexpr const char *TOPIC_CMD_MIN_FREQUENCY = "homeassistant/nebulizador/number/min_frequency_min/set";
constexpr const char *TOPIC_CMD_VALVE_ACTIVE = "homeassistant/nebulizador/number/valve_active_s/set";
constexpr const char *TOPIC_CMD_MANUAL = "homeassistant/nebulizador/switch/manual/set";
constexpr const char *TOPIC_CMD_START_TIME = "homeassistant/nebulizador/time/start_time/set";
constexpr const char *TOPIC_CMD_END_TIME = "homeassistant/nebulizador/time/end_time/set";

constexpr uint32_t MQTT_RECONNECT_INTERVAL_MS = 5000;

// HA device-based MQTT discovery: one retained payload registers every entity.
// min/max here must be kept in sync by hand with the clamp ranges in
// task_valve.cpp's setters — that's the actual enforcement, this is just what
// HA's UI offers the user.
const char DISCOVERY_PAYLOAD[] = R"JSON({
  "dev": { "ids": "nebulizador", "name": "Nebulizador", "mf": "DIY", "mdl": "Wemos D1 mini (ESP8266)" },
  "o": { "name": "nebulizador-firmware" },
  "state_topic": "homeassistant/nebulizador/state",
  "availability_topic": "homeassistant/nebulizador/availability",
  "qos": 1,
  "cmps": {
    "nebulizador_temperature": { "p": "sensor", "name": "Temperatura", "device_class": "temperature", "unit_of_measurement": "°C", "state_class": "measurement", "value_template": "{{ value_json.temperature }}", "unique_id": "nebulizador_temperature" },
    "nebulizador_humidity": { "p": "sensor", "name": "Humedad", "device_class": "humidity", "unit_of_measurement": "%", "state_class": "measurement", "value_template": "{{ value_json.humidity }}", "unique_id": "nebulizador_humidity" },
    "nebulizador_heat_index": { "p": "sensor", "name": "Sensación térmica", "device_class": "temperature", "unit_of_measurement": "°C", "state_class": "measurement", "value_template": "{{ value_json.heat_index }}", "unique_id": "nebulizador_heat_index" },
    "nebulizador_valve": { "p": "binary_sensor", "name": "Válvula", "device_class": "opening", "value_template": "{{ value_json.valve }}", "payload_on": "ON", "payload_off": "OFF", "unique_id": "nebulizador_valve" },
    "nebulizador_error": { "p": "binary_sensor", "name": "Error", "device_class": "problem", "value_template": "{{ 'ON' if (value_json.error_flags | int(0)) > 0 else 'OFF' }}", "unique_id": "nebulizador_error" },
    "nebulizador_error_flags": { "p": "sensor", "name": "Código de error", "entity_category": "diagnostic", "value_template": "{{ value_json.error_flags }}", "unique_id": "nebulizador_error_flags" },
    "nebulizador_min_hindex": { "p": "number", "name": "Umbral mínimo (sensación térmica)", "command_topic": "homeassistant/nebulizador/number/min_hindex/set", "value_template": "{{ value_json.min_hindex }}", "unit_of_measurement": "°C", "min": 15, "max": 45, "step": 0.1, "mode": "box", "entity_category": "config", "unique_id": "nebulizador_min_hindex" },
    "nebulizador_max_hindex": { "p": "number", "name": "Umbral máximo (sensación térmica)", "command_topic": "homeassistant/nebulizador/number/max_hindex/set", "value_template": "{{ value_json.max_hindex }}", "unit_of_measurement": "°C", "min": 20, "max": 50, "step": 0.1, "mode": "box", "entity_category": "config", "unique_id": "nebulizador_max_hindex" },
    "nebulizador_max_frequency": { "p": "number", "name": "Frecuencia máxima de chequeo", "command_topic": "homeassistant/nebulizador/number/max_frequency_min/set", "value_template": "{{ value_json.max_frequency_min }}", "unit_of_measurement": "min", "min": 1, "max": 120, "step": 1, "mode": "box", "entity_category": "config", "unique_id": "nebulizador_max_frequency" },
    "nebulizador_min_frequency": { "p": "number", "name": "Frecuencia mínima de chequeo", "command_topic": "homeassistant/nebulizador/number/min_frequency_min/set", "value_template": "{{ value_json.min_frequency_min }}", "unit_of_measurement": "min", "min": 1, "max": 60, "step": 1, "mode": "box", "entity_category": "config", "unique_id": "nebulizador_min_frequency" },
    "nebulizador_valve_active": { "p": "number", "name": "Segundos de válvula abierta", "command_topic": "homeassistant/nebulizador/number/valve_active_s/set", "value_template": "{{ value_json.valve_active_s }}", "unit_of_measurement": "s", "min": 1, "max": 60, "step": 1, "mode": "box", "entity_category": "config", "unique_id": "nebulizador_valve_active" },
    "nebulizador_next_run": { "p": "sensor", "name": "Próxima ejecución", "device_class": "duration", "unit_of_measurement": "s", "value_template": "{{ value_json.next_run_s }}", "unique_id": "nebulizador_next_run" },
    "nebulizador_last_run": { "p": "sensor", "name": "Última ejecución", "device_class": "timestamp", "value_template": "{{ value_json.last_run }}", "unique_id": "nebulizador_last_run" },
    "nebulizador_manual": { "p": "switch", "name": "Disparo manual", "command_topic": "homeassistant/nebulizador/switch/manual/set", "value_template": "{{ value_json.manual }}", "payload_on": "ON", "payload_off": "OFF", "unique_id": "nebulizador_manual" },
    "nebulizador_start_time": { "p": "time", "name": "Hora de inicio", "command_topic": "homeassistant/nebulizador/time/start_time/set", "value_template": "{{ value_json.start_time }}", "entity_category": "config", "unique_id": "nebulizador_start_time" },
    "nebulizador_end_time": { "p": "time", "name": "Hora de fin", "command_topic": "homeassistant/nebulizador/time/end_time/set", "value_template": "{{ value_json.end_time }}", "entity_category": "config", "unique_id": "nebulizador_end_time" }
  }
})JSON";

WiFiClient s_wifiClient;
PubSubClient s_mqtt(s_wifiClient);

unsigned long s_lastConnectAttempt = 0;

// Cached values to detect changes worth republishing. sensorValid is checked
// separately from heatIndex specifically to avoid comparing NAN != NAN (which
// is always true in IEEE754 and would otherwise republish every tick while a
// sensor error persists).
bool s_lastSensorValid = false;
float s_lastHeatIndex = NAN;
bool s_lastValveActive = false;
uint8_t s_lastErrors = 0xFF;
unsigned long s_lastCycleStartMs = 0;
unsigned long s_lastSensorIntervalMs = 0;
bool s_lastManualActive = false;

String buildStateJson() {
  bool valid = sensorsReadIsValid();

  String json = "{\"temperature\":";
  json += valid ? String(sensorsGetTemperature(), 1) : String("null");
  json += ",\"humidity\":";
  json += valid ? String(sensorsGetHumidity(), 1) : String("null");
  json += ",\"heat_index\":";
  json += valid ? String(sensorsGetHeatIndex(), 1) : String("null");
  json += ",\"valve\":\"";
  json += valveTaskIsActive() ? "ON" : "OFF";
  json += "\",\"error_flags\":";
  json += String(getErrors());
  json += ",\"min_hindex\":";
  json += String(valveTaskGetMinHIndexThreshold(), 1);
  json += ",\"max_hindex\":";
  json += String(valveTaskGetMaxHIndexThreshold(), 1);
  json += ",\"max_frequency_min\":";
  json += String(valveTaskGetMaxFrequencyMs() / 60000UL);
  json += ",\"min_frequency_min\":";
  json += String(valveTaskGetMinFrequencyMs() / 60000UL);
  json += ",\"valve_active_s\":";
  json += String(valveTaskGetValveActiveTimeMs() / 1000UL);
  json += ",\"next_run_s\":";
  json += String(valveTaskGetSensorIntervalMs() / 1000UL);
  json += ",\"last_run\":";
  if (timeTaskIsSynced()) {
    unsigned long elapsedMs = millis() - valveTaskGetCycleStartMs(); // unsigned sub — correct across millis() rollover
    time_t lastRunEpoch = time(nullptr) - static_cast<time_t>(elapsedMs / 1000UL);
    char isoBuf[24];
    timeTaskFormatEpochUtc(lastRunEpoch, isoBuf, sizeof(isoBuf));
    json += "\"";
    json += isoBuf;
    json += "\"";
  } else {
    json += "null"; // HA renders a JSON null as 'None' -> the timestamp sensor shows "unknown"
  }
  json += ",\"manual\":\"";
  json += valveTaskManualIsActive() ? "ON" : "OFF";
  json += "\",\"start_time\":\"";
  {
    uint16_t m = valveTaskGetStartMinuteOfDay();
    char timeBuf[9];
    snprintf(timeBuf, sizeof(timeBuf), "%02u:%02u:00", m / 60, m % 60);
    json += timeBuf;
  }
  json += "\",\"end_time\":\"";
  {
    uint16_t m = valveTaskGetEndMinuteOfDay();
    char timeBuf[9];
    snprintf(timeBuf, sizeof(timeBuf), "%02u:%02u:00", m / 60, m % 60);
    json += timeBuf;
  }
  json += "\"}";
  return json;
}

void publishState() { s_mqtt.publish(TOPIC_STATE, buildStateJson().c_str(), true); }

void publishAvailability(bool online) { s_mqtt.publish(TOPIC_AVAILABILITY, online ? "online" : "offline", true); }

void mqttCallback(char *topic, uint8_t *payload, unsigned int length) {
  char buf[16];
  unsigned int n = length < sizeof(buf) - 1 ? length : sizeof(buf) - 1;
  memcpy(buf, payload, n);
  buf[n] = '\0';

  String t(topic);
  bool handled = true;

  if (t == TOPIC_CMD_MIN_HINDEX) {
    valveTaskSetMinHIndexThreshold(atof(buf));
  } else if (t == TOPIC_CMD_MAX_HINDEX) {
    valveTaskSetMaxHIndexThreshold(atof(buf));
  } else if (t == TOPIC_CMD_MAX_FREQUENCY) {
    valveTaskSetMaxFrequencyMs(strtoul(buf, nullptr, 10) * 60000UL);
  } else if (t == TOPIC_CMD_MIN_FREQUENCY) {
    valveTaskSetMinFrequencyMs(strtoul(buf, nullptr, 10) * 60000UL);
  } else if (t == TOPIC_CMD_VALVE_ACTIVE) {
    valveTaskSetValveActiveTimeMs(strtoul(buf, nullptr, 10) * 1000UL);
  } else {
    handled = false;
  }

  if (handled) {
    Serial.print(F("[MQTT] Applied command on "));
    Serial.println(topic);
    publishState(); // Confirm the (possibly clamped) resulting value back to HA immediately.
  }
}
} // namespace

void mqttTaskBegin() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  s_mqtt.setServer(MQTT_SERVER, MQTT_PORT);
  s_mqtt.setBufferSize(5120); // Discovery payload measures ~3.5KB; ~45% headroom for future entities.
  s_mqtt.setSocketTimeout(5);
  s_mqtt.setCallback(mqttCallback);
}

void mqttTaskLoop(unsigned long now) {
  if (WiFi.status() != WL_CONNECTED)
    return;

  if (!s_mqtt.connected()) {
    if (now - s_lastConnectAttempt < MQTT_RECONNECT_INTERVAL_MS)
      return;
    s_lastConnectAttempt = now;

    Serial.println(F("[MQTT] Connecting..."));
    bool ok = s_mqtt.connect(DEVICE_ID, MQTT_USER, MQTT_PASSWORD, TOPIC_AVAILABILITY, 0, true, "offline");
    if (!ok) {
      Serial.print(F("[MQTT] Connect failed, rc="));
      Serial.println(s_mqtt.state());
      return;
    }

    Serial.println(F("[MQTT] Connected"));
    // Republished on every (re)connect, not just cold boot — idempotent, and
    // self-heals a broker that lost its retained messages.
    s_mqtt.publish(TOPIC_DISCOVERY, DISCOVERY_PAYLOAD, true);
    publishAvailability(true);
    publishState();

    s_mqtt.subscribe(TOPIC_CMD_MIN_HINDEX);
    s_mqtt.subscribe(TOPIC_CMD_MAX_HINDEX);
    s_mqtt.subscribe(TOPIC_CMD_MAX_FREQUENCY);
    s_mqtt.subscribe(TOPIC_CMD_MIN_FREQUENCY);
    s_mqtt.subscribe(TOPIC_CMD_VALVE_ACTIVE);

    s_lastSensorValid = sensorsReadIsValid();
    s_lastHeatIndex = sensorsGetHeatIndex();
    s_lastValveActive = valveTaskIsActive();
    s_lastErrors = getErrors();
    return;
  }

  s_mqtt.loop();

  bool sensorValid = sensorsReadIsValid();
  float heatIndex = sensorsGetHeatIndex();
  bool valveActive = valveTaskIsActive();
  uint8_t errors = getErrors();

  bool changed = (sensorValid != s_lastSensorValid) || (sensorValid && heatIndex != s_lastHeatIndex) ||
                 (valveActive != s_lastValveActive) || (errors != s_lastErrors);

  if (changed) {
    publishState();
    s_lastSensorValid = sensorValid;
    s_lastHeatIndex = heatIndex;
    s_lastValveActive = valveActive;
    s_lastErrors = errors;
  }
}

void mqttTaskDisconnect() {
  if (!s_mqtt.connected())
    return;
  publishAvailability(false);
  s_mqtt.disconnect();
}
