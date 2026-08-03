#pragma once

#include <string>
#include <vector>

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/kegboard/kegboard.h"
#include "esphome/components/kegboard/pour_session.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/gpio.h"
#include "esphome/core/helpers.h"

namespace esphome::kegboard_meter {

class PourStartTrigger : public Trigger<> {};

/// Fires with (ticks, volume_ml, duration_ms).
class PourEndTrigger : public Trigger<uint32_t, float, uint32_t> {};

/// One flow meter: counts pulses and turns them into pours.
///
/// Counting happens in an ISR; everything else runs in the main loop, where
/// the accumulated ticks are handed to a kbcore::PourSession that owns all the
/// actual pour-detection logic. Keeping the state machine out of here is what
/// lets it be unit tested on a host.
class KegboardMeter : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void set_hub(kegboard::KegboardHub *hub) { this->hub_ = hub; }
  void set_pin(InternalGPIOPin *pin) { this->pin_ = pin; }
  void set_index(uint8_t index) { this->index_ = index; }
  void set_meter_name(const std::string &name) { this->meter_name_ = name; }
  void set_filter_us(uint32_t filter_us) { this->filter_us_ = filter_us; }
  void set_report_interval_ms(uint32_t interval) { this->report_interval_ms_ = interval; }

  void set_idle_timeout_ms(uint32_t v) { this->pour_config_.idle_timeout_ms = v; }
  void set_min_pour_ticks(uint32_t v) { this->pour_config_.min_pour_ticks = v; }
  void set_max_duration_ms(uint32_t v) { this->pour_config_.max_duration_ms = v; }
  void set_ml_per_tick(float v);
  void set_series_resolution_ms(uint32_t v) { this->series_resolution_ms_ = v; }

  void set_total_sensor(sensor::Sensor *s) { this->total_sensor_ = s; }
  void set_volume_sensor(sensor::Sensor *s) { this->volume_sensor_ = s; }
  void set_flow_rate_sensor(sensor::Sensor *s) { this->flow_rate_sensor_ = s; }
  void set_pouring_binary_sensor(binary_sensor::BinarySensor *s) { this->pouring_sensor_ = s; }

  void add_on_pour_start_trigger(Trigger<> *trigger) { this->pour_start_triggers_.push_back(trigger); }
  void add_on_pour_end_trigger(Trigger<uint32_t, float, uint32_t> *trigger) {
    this->pour_end_triggers_.push_back(trigger);
  }

  /// Called with every completed pour, as (record, meter name, username).
  /// Reporters subscribe here rather than being wired through YAML
  /// automations, so a pour cannot be silently dropped by a missing
  /// `on_pour_end:` block.
  void add_on_pour_callback(
      std::function<void(const kbcore::PourRecord &, const std::string &, const std::string &)> &&callback) {
    this->pour_callbacks_.add(std::move(callback));
  }

  /// Kegbot user this meter's pours are currently attributed to, set by
  /// kegboard_auth when a token is presented. Empty means the pour is
  /// recorded against the guest user.
  void set_active_username(const std::string &username) { this->active_username_ = username; }
  const std::string &active_username() const { return this->active_username_; }

  bool is_pouring() const { return this->session_.is_pouring(); }

  /// Meter name as Kegbot Server knows it, e.g. "kegboard-a1b2c3.flow0".
  /// Resolved on first use, since the hub's serial number is only known after
  /// its setup() has run.
  const std::string &meter_name();

  float ml_per_tick() const { return this->pour_config_.ml_per_tick; }
  uint32_t total_ticks() const { return this->session_.total_ticks(); }

  void reset_total();

  /// End any pour in progress immediately, reporting it if it qualifies.
  void end_pour();

 protected:
  static void gpio_intr(KegboardMeter *meter);

  uint32_t take_isr_ticks_();
  void publish_state_(bool force);
  void handle_pour_end_(const kbcore::PourRecord &record);

  kegboard::KegboardHub *hub_{nullptr};
  InternalGPIOPin *pin_{nullptr};
  uint8_t index_{0};
  std::string meter_name_;

  kbcore::PourConfig pour_config_;
  kbcore::PourSession session_{kbcore::PourConfig{}};
  uint32_t series_resolution_ms_{kbcore::TickSeries::DEFAULT_RESOLUTION_MS};

  sensor::Sensor *total_sensor_{nullptr};
  sensor::Sensor *volume_sensor_{nullptr};
  sensor::Sensor *flow_rate_sensor_{nullptr};
  binary_sensor::BinarySensor *pouring_sensor_{nullptr};

  std::string active_username_;

  std::vector<Trigger<> *> pour_start_triggers_;
  std::vector<Trigger<uint32_t, float, uint32_t> *> pour_end_triggers_;
  CallbackManager<void(const kbcore::PourRecord &, const std::string &, const std::string &)> pour_callbacks_;

  // Written by the ISR, drained by loop() under an InterruptLock.
  volatile uint32_t isr_ticks_{0};
  volatile uint32_t last_edge_us_{0};
  uint32_t filter_us_{1200};

  uint32_t report_interval_ms_{250};
  uint32_t last_report_ms_{0};
  uint32_t ticks_since_report_{0};
  bool was_pouring_{false};
};

template<typename... Ts> class ResetTotalAction : public Action<Ts...>, public Parented<KegboardMeter> {
 public:
  void play(const Ts &...x) override { this->parent_->reset_total(); }
};

template<typename... Ts> class EndPourAction : public Action<Ts...>, public Parented<KegboardMeter> {
 public:
  void play(const Ts &...x) override { this->parent_->end_pour(); }
};

template<typename... Ts> class SetCalibrationAction : public Action<Ts...>, public Parented<KegboardMeter> {
 public:
  TEMPLATABLE_VALUE(float, ml_per_tick)

  void play(const Ts &...x) override { this->parent_->set_ml_per_tick(this->ml_per_tick_.value(x...)); }
};

}  // namespace esphome::kegboard_meter
