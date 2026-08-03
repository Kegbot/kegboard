#include "kegboard_onewire.h"

#include <cinttypes>
#include <cstdio>

#include "esphome/core/log.h"

namespace esphome::kegboard_onewire {

static const char *const TAG = "kegboard_onewire";

void KegboardOneWire::setup() {
  if (this->bus_ == nullptr) {
    this->mark_failed();
    return;
  }
}

std::string KegboardOneWire::format_token_(uint64_t address) {
  char buf[17];
  snprintf(buf, sizeof(buf), "%016" PRIx64, address);
  return std::string(buf);
}

void KegboardOneWire::update() {
  this->bus_->search();
  const std::vector<uint64_t> &found = this->bus_->get_devices();

  // Age every known device, then clear the counter for any still present. A
  // device seen this round is at zero misses; one absent for too many rounds
  // is gone.
  for (auto &entry : this->present_)
    entry.misses++;

  for (uint64_t address : found) {
    if (address == 0)
      continue;

    bool known = false;
    for (auto &entry : this->present_) {
      if (entry.address == address) {
        entry.misses = 0;
        known = true;
        break;
      }
    }
    if (known)
      continue;

    this->present_.push_back(Entry{address, 0});
    const std::string token = format_token_(address);
    ESP_LOGI(TAG, "Token attached: %s", token.c_str());
    for (auto *trigger : this->attached_triggers_)
      trigger->trigger(token);
  }

  for (auto it = this->present_.begin(); it != this->present_.end();) {
    if (it->misses <= this->max_missed_searches_) {
      ++it;
      continue;
    }
    const std::string token = format_token_(it->address);
    ESP_LOGI(TAG, "Token detached: %s", token.c_str());
    for (auto *trigger : this->detached_triggers_)
      trigger->trigger(token);
    it = this->present_.erase(it);
  }
}

void KegboardOneWire::dump_config() {
  ESP_LOGCONFIG(TAG, "Kegboard 1-Wire auth:");
  ESP_LOGCONFIG(TAG, "  Missed searches before detach: %u", this->max_missed_searches_);
  LOG_UPDATE_INTERVAL(this);
  if (this->is_failed()) {
    ESP_LOGE(TAG, "  No 1-Wire bus configured");
  }
}

}  // namespace esphome::kegboard_onewire
