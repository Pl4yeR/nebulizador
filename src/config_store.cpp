#include "config_store.h"

#include <LittleFS.h>

namespace {
constexpr uint32_t MAGIC = 0x4E425A31; // "NBZ1"
constexpr uint16_t SCHEMA_VERSION = 1;
constexpr const char *CONFIG_PATH = "/valve_config.bin";
constexpr const char *CONFIG_TMP_PATH = "/valve_config.bin.tmp";

struct StoredConfig {
  uint32_t magic;
  uint16_t version;
  ValveConfig config;
};

bool s_mounted = false;

void ensureMounted() {
  if (s_mounted)
    return;
  s_mounted = LittleFS.begin();
  if (!s_mounted) {
    Serial.println(F("[CONFIG] LittleFS mount failed"));
  }
}
} // namespace

bool configStoreLoadValveConfig(ValveConfig &out) {
  ensureMounted();
  if (!s_mounted)
    return false;

  File f = LittleFS.open(CONFIG_PATH, "r");
  if (!f)
    return false;

  StoredConfig stored;
  size_t n = f.readBytes(reinterpret_cast<char *>(&stored), sizeof(stored));
  f.close();

  if (n != sizeof(stored) || stored.magic != MAGIC || stored.version != SCHEMA_VERSION) {
    Serial.println(F("[CONFIG] Stored config missing/corrupt/outdated"));
    return false;
  }

  out = stored.config;
  return true;
}

bool configStoreSaveValveConfig(const ValveConfig &cfg) {
  ensureMounted();
  if (!s_mounted)
    return false;

  StoredConfig stored;
  stored.magic = MAGIC;
  stored.version = SCHEMA_VERSION;
  stored.config = cfg;

  File f = LittleFS.open(CONFIG_TMP_PATH, "w");
  if (!f) {
    Serial.println(F("[CONFIG] Failed to open temp config file for writing"));
    return false;
  }

  size_t written = f.write(reinterpret_cast<const uint8_t *>(&stored), sizeof(stored));
  f.close();

  if (written != sizeof(stored)) {
    Serial.println(F("[CONFIG] Short write to temp config file"));
    LittleFS.remove(CONFIG_TMP_PATH);
    return false;
  }

  // lfs_rename() atomically replaces an existing destination — no window
  // where neither file is valid, even if power is cut mid-save.
  if (!LittleFS.rename(CONFIG_TMP_PATH, CONFIG_PATH)) {
    Serial.println(F("[CONFIG] Failed to rename temp config file into place"));
    return false;
  }

  return true;
}
