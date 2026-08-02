#include "kegboard.h"

#include <cstdio>

#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome::kegboard {

static const char *const TAG = "kegboard";

const char *const KEGBOARD_VERSION = "2.0.0-dev";

void KegboardHub::setup() {
  if (!this->serial_number_.empty())
    return;

  // Derive a stable identity from the last three MAC bytes. Kegbot Server
  // keys meters by name, so this needs to survive reflashing and stay unique
  // across boards on the same network; the MAC gives both for free.
  uint8_t mac[6];
  get_mac_address_raw(mac);

  char buf[24];
  snprintf(buf, sizeof(buf), "kegboard-%02x%02x%02x", mac[3], mac[4], mac[5]);
  this->serial_number_ = buf;
}

void KegboardHub::dump_config() {
  ESP_LOGCONFIG(TAG, "Kegboard:");
  ESP_LOGCONFIG(TAG, "  Version: %s", KEGBOARD_VERSION);
  ESP_LOGCONFIG(TAG, "  Serial number: %s", this->serial_number_.c_str());
}

}  // namespace esphome::kegboard
