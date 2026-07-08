#include "task_ota.h"

#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <ElegantOTA.h>

#define ESP_MRD_USE_EEPROM true
#define MRD_TIMES 3
#define MRD_TIMEOUT 10
#define MRD_ADDRESS 0
#include <ESP_MultiResetDetector.h>

#include "config.h"
#include "task_led.h"
#include "task_mqtt.h"
#include "task_valve.h"

namespace {
constexpr uint32_t WIFI_TIMEOUT_MS = 15000;

MultiResetDetector *s_mrd = nullptr;
ESP8266WebServer s_server(80);
} // namespace

bool otaTaskCheckTrigger() {
  s_mrd = new MultiResetDetector(MRD_TIMEOUT, MRD_ADDRESS);

  if (s_mrd->detectMultiReset()) {
    Serial.println(F("[OTA] Triple reset detected"));
    s_mrd->stop();
    return true;
  }

  return false;
}

void otaTaskLoop() {
  // MultiResetDetector::loop() owns its own millis()-based timeout and calls
  // stop() internally once the reset-detection window (MRD_TIMEOUT) elapses —
  // no need to track that here.
  s_mrd->loop();
}

void otaTaskEnter() {
  Serial.println(F("[OTA] Entering OTA mode"));
  ledTaskSetOtaMode();
  valveTaskForceClose();
  mqttTaskDisconnect(); // No-op if task_mqtt never began this boot (e.g. triple-reset at cold boot)

  if (WiFi.status() == WL_CONNECTED) {
    // A prior normal-mode boot may have already connected WiFi (persisted STA
    // credentials + the SDK's own auto-reconnect) even though task_mqtt never
    // ran this boot — reuse it instead of tearing it down and reconnecting.
    Serial.println(F("[OTA] WiFi already connected — reusing"));
  } else {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.printf("[OTA] Connecting to %s\n", WIFI_SSID);

    unsigned long t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < WIFI_TIMEOUT_MS) {
      delay(250);
    }
  }

  String ip;
  if (WiFi.status() == WL_CONNECTED) {
    ip = WiFi.localIP().toString();
    Serial.printf("[OTA] Connected — IP: %s\n", ip.c_str());
  } else {
    Serial.println(F("[OTA] WiFi failed — starting AP"));
    WiFi.mode(WIFI_AP);
    String apName = "Nebulizador-OTA-" + String(ESP.getChipId(), HEX);
    WiFi.softAP(apName.c_str(), OTA_AP_PASSWORD);
    ip = WiFi.softAPIP().toString();
    Serial.printf("[OTA] AP: %s  IP: %s\n", apName.c_str(), ip.c_str());
  }

  s_server.on("/", []() {
    s_server.send(200, "text/html",
                  F("<html><body>Hi! This is Nebulizador OTA update page. Go to "
                    "<a href=\"/update\">/update</a> to start.</body></html>"));
  });

  ElegantOTA.begin(&s_server, "admin", OTA_AP_PASSWORD);
  s_server.begin();
  Serial.printf("[OTA] Ready at http://%s/update\n", ip.c_str());

  for (;;) {
    s_server.handleClient();
    ElegantOTA.loop();
    ledTaskLoop(millis());
    delay(2);
  }
}
