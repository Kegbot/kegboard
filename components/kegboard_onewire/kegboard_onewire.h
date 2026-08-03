#pragma once

#include <string>
#include <vector>

#include "esphome/components/one_wire/one_wire_bus.h"
#include "esphome/core/automation.h"
#include "esphome/core/component.h"

namespace esphome::kegboard_onewire {

/// Fires with the token id as a lowercase hex string.
class TokenAttachedTrigger : public Trigger<std::string> {};
class TokenDetachedTrigger : public Trigger<std::string> {};

/// Tracks iButtons touched to a 1-Wire bus.
///
/// ESPHome's one_wire bus can enumerate devices, but has no notion of a device
/// arriving or leaving, which is the whole point of an iButton reader. This
/// adds that: repeated searches, with an appearance reported once and a
/// disappearance only after several consecutive misses.
///
/// The miss counter matters more than it looks. A finger-held iButton makes
/// intermittent contact, and a single dropped search is normal; reporting a
/// detach on the first miss would make a held token flap between attached and
/// detached several times a second. The AVR firmware used four missed searches
/// and that number has a decade of beer behind it, so it is the default here.
class KegboardOneWire : public PollingComponent {
 public:
  void setup() override;
  void update() override;
  void dump_config() override;

  void set_bus(one_wire::OneWireBus *bus) { this->bus_ = bus; }
  void set_max_missed_searches(uint8_t count) { this->max_missed_searches_ = count; }

  void add_on_attached_trigger(TokenAttachedTrigger *trigger) { this->attached_triggers_.push_back(trigger); }
  void add_on_detached_trigger(TokenDetachedTrigger *trigger) { this->detached_triggers_.push_back(trigger); }

  /// Device name reported alongside the token, matching the legacy firmware's
  /// value so an existing Kegbot Server recognises the tokens.
  static const char *device_name() { return "onewire"; }

 protected:
  struct Entry {
    uint64_t address;
    uint8_t misses;
  };

  /// Kegbot stores iButton tokens as the 16-hex-digit ROM code.
  static std::string format_token_(uint64_t address);

  one_wire::OneWireBus *bus_{nullptr};
  uint8_t max_missed_searches_{4};
  std::vector<Entry> present_;

  std::vector<TokenAttachedTrigger *> attached_triggers_;
  std::vector<TokenDetachedTrigger *> detached_triggers_;
};

}  // namespace esphome::kegboard_onewire
