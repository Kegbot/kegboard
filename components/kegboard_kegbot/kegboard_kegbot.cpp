#include "kegboard_kegbot.h"

#include <cinttypes>
#include <cmath>

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome::kegboard_kegbot {

static const char *const TAG = "kegboard_kegbot";

/// Backoff cap. Long enough not to hammer a server that is down, short enough
/// that a keg party does not spend an hour with a stale tap list.
static constexpr uint32_t MAX_RETRY_INTERVAL_MS = 300000;

void KegbotReporter::setup() {
  // Give the rest of the firmware a wall clock without making `time` a
  // dependency of every component. See KegboardHub::set_clock_source().
  if (this->hub_ != nullptr && this->time_ != nullptr) {
    auto *clock = this->time_;
    this->hub_->set_clock_source([clock]() -> uint32_t {
      auto now = clock->now();
      return now.is_valid() ? static_cast<uint32_t>(now.timestamp) : 0;
    });
  }
  this->publish_diagnostics_();
}

void KegbotReporter::add_meter(kegboard_meter::KegboardMeter *meter) {
  meter->add_on_pour_callback([this](const kbcore::PourRecord &record, const std::string &meter_name) {
    this->queue_drink_(record, meter_name);
  });
}

void KegbotReporter::add_thermo_sensor(sensor::Sensor *sensor, const std::string &name) {
  sensor->add_on_state_callback([this, name](float value) {
    if (!std::isnan(value))
      this->queue_thermo_(name, value);
  });
}

uint32_t KegbotReporter::now_unix_() const {
  if (this->time_ == nullptr)
    return 0;
  auto now = this->time_->now();
  return now.is_valid() ? static_cast<uint32_t>(now.timestamp) : 0;
}

uint32_t KegbotReporter::uptime_s_() const { return millis() / 1000; }

void KegbotReporter::queue_drink_(const kbcore::PourRecord &record, const std::string &meter_name) {
  kbcore::DrinkReport report;
  report.meter_name = meter_name;
  report.ticks = record.ticks;
  report.volume_ml = record.volume_ml;
  report.duration_s = (record.duration_ms + 500) / 1000;
  report.pour_time_unix = record.start_unix;
  report.tick_time_series = record.series.to_string();

  // The pour started duration_ms ago, so its uptime stamp is now minus that.
  // This pair is what makes a delayed delivery land at the right time.
  const uint32_t uptime = this->uptime_s_();
  const uint32_t age_s = record.duration_ms / 1000;
  report.pour_uptime_s = uptime > age_s ? uptime - age_s : 0;

  if (!this->drinks_.push(report))
    ESP_LOGW(TAG, "Drink queue full; dropped the oldest pour (%" PRIu32 " total)", this->drinks_.dropped());

  ESP_LOGD(TAG, "Queued pour on %s: %" PRIu32 " ticks (%u queued)", meter_name.c_str(), record.ticks,
           static_cast<unsigned>(this->drinks_.size()));

  // A pour is worth an immediate attempt rather than waiting out a backoff
  // left over from an earlier failure.
  this->next_attempt_ms_ = millis();
  this->publish_diagnostics_();
}

void KegbotReporter::queue_thermo_(const std::string &name, float temp_c) {
  kbcore::ThermoReport report;
  report.sensor_name = name;
  report.temp_c = temp_c;
  report.when_unix = this->now_unix_();
  report.when_uptime_s = this->uptime_s_();
  this->thermos_.push(report);
}

void KegbotReporter::loop() {
  if (this->drinks_.empty() && this->thermos_.empty())
    return;

  const uint32_t now = millis();
  if (static_cast<int32_t>(now - this->next_attempt_ms_) < 0)
    return;

  if (this->http_request_ == nullptr)
    return;

  // One item per loop iteration: each send blocks, so draining a backlog all
  // at once would stall meter polling and lose ticks.
  this->process_queue_();
  this->publish_diagnostics_();
}

bool KegbotReporter::process_queue_() {
  const uint32_t now_unix = this->now_unix_();
  const uint32_t uptime = this->uptime_s_();

  // Pours first: they are the data that cannot be regenerated.
  bool is_drink = !this->drinks_.empty();
  kbcore::HttpCall call;

  if (is_drink) {
    call = this->builder_.drink_post(*this->drinks_.peek(), now_unix, uptime);
  } else {
    call = this->builder_.thermo_post(*this->thermos_.peek(), now_unix, uptime);
  }

  if (this->send_(call)) {
    if (is_drink) {
      this->drinks_.pop();
    } else {
      this->thermos_.pop();
    }
    this->consecutive_failures_ = 0;
    this->next_attempt_ms_ = millis();
    return true;
  }

  // Exponential backoff, capped. Retrying a dead server every loop achieves
  // nothing except burning the connection and the log.
  this->consecutive_failures_++;
  uint32_t delay_ms = this->retry_interval_ms_;
  for (uint32_t i = 1; i < this->consecutive_failures_ && delay_ms < MAX_RETRY_INTERVAL_MS; i++)
    delay_ms *= 2;
  if (delay_ms > MAX_RETRY_INTERVAL_MS)
    delay_ms = MAX_RETRY_INTERVAL_MS;

  this->next_attempt_ms_ = millis() + delay_ms;
  ESP_LOGW(TAG, "Delivery failed (%" PRIu32 " in a row); retrying in %" PRIu32 " s", this->consecutive_failures_,
           delay_ms / 1000);
  return false;
}

bool KegbotReporter::send_(const kbcore::HttpCall &call) {
  std::vector<http_request::Header> headers;
  headers.push_back({"X-Kegbot-Api-Key", this->api_key_});
  if (call.method == "POST")
    headers.push_back({"Content-Type", "application/x-www-form-urlencoded"});

  auto container = this->http_request_->start(call.url, call.method, call.body, headers);
  if (container == nullptr) {
    ESP_LOGW(TAG, "%s %s: no response", call.method.c_str(), call.url.c_str());
    return false;
  }

  const int status = container->status_code;
  container->end();

  if (http_request::is_success(status)) {
    ESP_LOGD(TAG, "%s %s -> %d", call.method.c_str(), call.url.c_str(), status);
    return true;
  }

  // A 4xx means the server understood us and refused: a bad API key, an
  // unknown meter, or a tap with no keg. Retrying cannot fix any of those, and
  // an unknown meter is the normal state before a tap is configured, so the
  // item is discarded rather than blocking the queue forever.
  if (status >= 400 && status < 500) {
    ESP_LOGE(TAG, "%s %s -> %d; discarding (check api key and that the meter is connected to a tap)",
             call.method.c_str(), call.url.c_str(), status);
    return true;
  }

  ESP_LOGW(TAG, "%s %s -> %d", call.method.c_str(), call.url.c_str(), status);
  return false;
}

void KegbotReporter::publish_diagnostics_() {
  const uint32_t depth = this->drinks_.size() + this->thermos_.size();
  const uint32_t dropped = this->drinks_.dropped() + this->thermos_.dropped();

  if (this->queue_depth_sensor_ != nullptr && depth != this->last_published_depth_) {
    this->queue_depth_sensor_->publish_state(depth);
    this->last_published_depth_ = depth;
  }
  if (this->dropped_sensor_ != nullptr && dropped != this->last_published_dropped_) {
    this->dropped_sensor_->publish_state(dropped);
    this->last_published_dropped_ = dropped;
  }
}

void KegbotReporter::dump_config() {
  ESP_LOGCONFIG(TAG, "Kegbot Reporter:");
  ESP_LOGCONFIG(TAG, "  Server: %s", this->builder_.base_url().c_str());
  ESP_LOGCONFIG(TAG, "  API key: %s", this->api_key_.empty() ? "NOT SET" : "set");
  ESP_LOGCONFIG(TAG, "  Send volume: %s", YESNO(this->builder_.send_volume()));
  ESP_LOGCONFIG(TAG, "  Retry interval: %" PRIu32 " s", this->retry_interval_ms_ / 1000);
  ESP_LOGCONFIG(TAG, "  Queue capacity: %u drinks, %u readings", static_cast<unsigned>(DRINK_QUEUE_SIZE),
                static_cast<unsigned>(THERMO_QUEUE_SIZE));
  if (this->time_ == nullptr) {
    ESP_LOGCONFIG(TAG, "  Clock: none (pours will be reported by elapsed time)");
  }
}

}  // namespace esphome::kegboard_kegbot
