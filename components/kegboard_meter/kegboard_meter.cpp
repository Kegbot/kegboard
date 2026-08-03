#include "kegboard_meter.h"

#include "esphome/components/kegboard/events.h"

#include <cinttypes>

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome::kegboard_meter {

static const char *const TAG = "kegboard_meter";

void KegboardMeter::gpio_intr(KegboardMeter *meter) {
  const uint32_t now = micros();

  // Flow meter reed and hall sensors bounce. Anything arriving sooner than the
  // filter window is the same edge ringing, and counting it would inflate the
  // pour. Unsigned subtraction keeps this correct across the micros() wrap.
  if (now - meter->last_edge_us_ < meter->filter_us_)
    return;

  meter->last_edge_us_ = now;
  meter->isr_ticks_++;
}

void KegboardMeter::setup() {
  this->session_.set_config(this->pour_config_);
  this->session_.set_series_resolution_ms(this->series_resolution_ms_);

  this->pin_->setup();
  this->last_edge_us_ = micros();
  this->pin_->attach_interrupt(KegboardMeter::gpio_intr, this, gpio::INTERRUPT_FALLING_EDGE);

  this->last_report_ms_ = millis();
  this->publish_state_(true);
}

uint32_t KegboardMeter::take_isr_ticks_() {
  uint32_t ticks;
  {
#ifndef USE_HOST
    // Guarded because the host platform has no interrupts to mask, and so no
    // InterruptLock implementation. There the "ISR" never runs either, so the
    // unlocked read is equally correct.
    InterruptLock lock;
#endif
    ticks = this->isr_ticks_;
    this->isr_ticks_ = 0;
  }
  return ticks;
}

void KegboardMeter::set_ml_per_tick(float v) {
  this->pour_config_.ml_per_tick = v;
  this->session_.set_ml_per_tick(v);
}

void KegboardMeter::reset_total() {
  this->session_.reset_total();
  this->publish_state_(true);
}

void KegboardMeter::end_pour() {
  kbcore::PourRecord record;
  if (this->session_.end_now(millis(), &record))
    this->handle_pour_end_(record);
  this->publish_state_(true);
}

void KegboardMeter::loop() {
  const uint32_t ticks = this->take_isr_ticks_();
  const uint32_t now_ms = millis();

  if (ticks > 0) {
    this->ticks_since_report_ += ticks;
    if (this->session_.add_ticks(ticks, now_ms, this->hub_ != nullptr ? this->hub_->now_unix() : 0)) {
      uint8_t random[16];
      random_bytes(random, sizeof(random));
      this->pour_id_ = kbcore::format_uuid4(random);
      ESP_LOGD(TAG, "meter %u: pour started (%s)", this->meter_number_, this->pour_id_.c_str());
      for (auto *trigger : this->pour_start_triggers_)
        trigger->trigger();
      this->publish_state_(true);
    }
  }

  kbcore::PourRecord record;
  if (this->session_.poll(now_ms, &record)) {
    this->handle_pour_end_(record);
    this->publish_state_(true);
    return;
  }

  this->publish_state_(false);
}

void KegboardMeter::handle_pour_end_(const kbcore::PourRecord &record) {
  ESP_LOGI(TAG, "meter %u: pour ended, %" PRIu32 " ticks (%.1f mL) in %" PRIu32 " ms", this->meter_number_,
           record.ticks, record.volume_ml, record.duration_ms);

  for (auto *trigger : this->pour_end_triggers_)
    trigger->trigger(record.ticks, record.volume_ml, record.duration_ms);

  this->pour_callbacks_.call(*this, record);
}

void KegboardMeter::publish_state_(bool force) {
  const uint32_t now_ms = millis();
  const bool pouring = this->session_.is_pouring();

  // A pour starting or ending is always worth reporting immediately; the
  // stream of updates during a pour is throttled so a fast meter does not
  // flood the API connection.
  if (pouring != this->was_pouring_)
    force = true;

  if (!force && (now_ms - this->last_report_ms_) < this->report_interval_ms_)
    return;

  const uint32_t elapsed_ms = now_ms - this->last_report_ms_;
  this->last_report_ms_ = now_ms;
  this->was_pouring_ = pouring;

  if (this->total_sensor_ != nullptr)
    this->total_sensor_->publish_state(this->session_.total_ticks());

  if (this->volume_sensor_ != nullptr)
    this->volume_sensor_->publish_state(this->session_.session_volume_ml());

  if (this->flow_rate_sensor_ != nullptr) {
    float rate = 0.0f;
    if (elapsed_ms > 0 && this->ticks_since_report_ > 0)
      rate = (this->ticks_since_report_ * this->pour_config_.ml_per_tick) * (60000.0f / elapsed_ms);
    this->flow_rate_sensor_->publish_state(rate);
  }
  this->ticks_since_report_ = 0;

  if (this->pouring_sensor_ != nullptr)
    this->pouring_sensor_->publish_state(pouring);
}

void KegboardMeter::dump_config() {
  ESP_LOGCONFIG(TAG, "Kegboard Meter %u:", this->meter_number_);
  LOG_PIN("  Pin: ", this->pin_);
  ESP_LOGCONFIG(TAG, "  Debounce: %" PRIu32 " us", this->filter_us_);
  ESP_LOGCONFIG(TAG, "  Calibration: %.4f mL/tick", this->pour_config_.ml_per_tick);
  ESP_LOGCONFIG(TAG, "  Idle timeout: %" PRIu32 " ms", this->pour_config_.idle_timeout_ms);
  ESP_LOGCONFIG(TAG, "  Minimum pour: %" PRIu32 " ticks", this->pour_config_.min_pour_ticks);
  ESP_LOGCONFIG(TAG, "  Maximum duration: %" PRIu32 " ms", this->pour_config_.max_duration_ms);
  LOG_SENSOR("  ", "Total", this->total_sensor_);
  LOG_SENSOR("  ", "Volume", this->volume_sensor_);
  LOG_SENSOR("  ", "Flow rate", this->flow_rate_sensor_);
  LOG_BINARY_SENSOR("  ", "Pouring", this->pouring_sensor_);
}

}  // namespace esphome::kegboard_meter
