#pragma once

#include <string>
#include <vector>

#include "esphome/components/http_request/http_request.h"
#include "esphome/components/kegboard/kegboard.h"
#include "esphome/components/kegboard/kegbot_request.h"
#include "esphome/components/kegboard/pour_session.h"
#include "esphome/components/kegboard/ring_queue.h"
#include "esphome/components/kegboard_meter/kegboard_meter.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/time/real_time_clock.h"
#include "esphome/core/component.h"

namespace esphome::kegboard_kegbot {

/// How many undelivered pours are held before the oldest is dropped. A pour is
/// a few hundred bytes, so this is cheap; the bound exists so a long outage
/// cannot exhaust the heap.
static constexpr size_t DRINK_QUEUE_SIZE = 16;

/// Temperature readings are far less precious than pours: they arrive
/// continuously and a missed one is replaced within seconds.
static constexpr size_t THERMO_QUEUE_SIZE = 4;

/// Reports pours and temperatures to a Kegbot Server.
///
/// Delivery is queue-first: a completed pour is enqueued, never posted
/// inline. That keeps the meter's loop responsive and means a server that is
/// down, slow, or unreachable costs a retry rather than a pour.
class KegbotReporter : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_CONNECTION; }

  void set_hub(kegboard::KegboardHub *hub) { this->hub_ = hub; }
  void set_http_request(http_request::HttpRequestComponent *parent) { this->http_request_ = parent; }
  void set_time(time::RealTimeClock *time) { this->time_ = time; }

  void set_base_url(const std::string &url) { this->builder_.set_base_url(url); }
  void set_api_key(const std::string &key) { this->api_key_ = key; }
  void set_send_volume(bool send_volume) { this->builder_.set_send_volume(send_volume); }
  void set_retry_interval_ms(uint32_t interval) { this->retry_interval_ms_ = interval; }

  void add_meter(kegboard_meter::KegboardMeter *meter);
  void add_thermo_sensor(sensor::Sensor *sensor, const std::string &name);

  void set_queue_depth_sensor(sensor::Sensor *s) { this->queue_depth_sensor_ = s; }
  void set_dropped_sensor(sensor::Sensor *s) { this->dropped_sensor_ = s; }

 protected:
  void queue_drink_(const kbcore::PourRecord &record, const std::string &meter_name);
  void queue_thermo_(const std::string &name, float temp_c);

  /// Send one queued item, if any. Returns true if the queue should be
  /// serviced again immediately.
  bool process_queue_();
  bool send_(const kbcore::HttpCall &call);

  uint32_t now_unix_() const;
  uint32_t uptime_s_() const;
  void publish_diagnostics_();

  kegboard::KegboardHub *hub_{nullptr};
  http_request::HttpRequestComponent *http_request_{nullptr};
  time::RealTimeClock *time_{nullptr};

  kbcore::KegbotRequestBuilder builder_;
  std::string api_key_;

  kbcore::RingQueue<kbcore::DrinkReport, DRINK_QUEUE_SIZE> drinks_;
  kbcore::RingQueue<kbcore::ThermoReport, THERMO_QUEUE_SIZE> thermos_;

  uint32_t retry_interval_ms_{30000};
  uint32_t next_attempt_ms_{0};
  uint32_t consecutive_failures_{0};

  sensor::Sensor *queue_depth_sensor_{nullptr};
  sensor::Sensor *dropped_sensor_{nullptr};
  uint32_t last_published_depth_{UINT32_MAX};
  uint32_t last_published_dropped_{UINT32_MAX};
};

}  // namespace esphome::kegboard_kegbot
