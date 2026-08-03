#pragma once

#include <functional>
#include <string>
#include <vector>

#include "esphome/components/http_request/http_request.h"
#include "esphome/components/json/json_util.h"
#include "esphome/components/kegboard/events.h"
#include "esphome/components/kegboard/kegboard.h"
#include "esphome/components/kegboard/ring_queue.h"
#include "esphome/components/kegboard_meter/kegboard_meter.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/time/real_time_clock.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"

namespace esphome::kegboard_reporter {

/// Outcome a command handler reports back, mirroring command_result.
enum class CommandOutcome : uint8_t { OK, ERROR, UNSUPPORTED };

/// Handler for server commands. Receives the command type and its `data`
/// object; returns the outcome and may set `message` for error detail.
using CommandHandler =
    std::function<CommandOutcome(const std::string &type, JsonObjectConst data, std::string &message)>;

/// Speaks the Kegboard Event Protocol (docs/kegboard-event-protocol.md).
///
/// Owns the event queue, batch delivery with backoff, the pairing state
/// machine, and command dispatch. Meters, sensors, and the auth component
/// feed events in; the auth component registers a command handler for
/// authorize/deny/deauthorize.
class KegboardReporter : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_CONNECTION; }

  void set_hub(kegboard::KegboardHub *hub) { this->hub_ = hub; }
  void set_http_request(http_request::HttpRequestComponent *h) { this->http_ = h; }
  void set_time(time::RealTimeClock *t) { this->time_ = t; }
  /// Full reporting URL, path included, e.g.
  /// "https://kegbot.example.com/api/kegboard-event". Used verbatim.
  void set_reporting_url(const std::string &url) { this->reporting_url_ = url; }
  void set_heartbeat_interval_ms(uint32_t v) { this->heartbeat_ms_ = v; }
  void set_pour_update_interval_ms(uint32_t v) { this->pour_update_ms_ = v; }
  void set_retry_interval_ms(uint32_t v) { this->retry_interval_ms_ = v; }

  void add_meter(kegboard_meter::KegboardMeter *meter);
  void add_thermo_sensor(sensor::Sensor *sensor, const std::string &name);

  void set_queue_depth_sensor(sensor::Sensor *s) { this->queue_depth_sensor_ = s; }
  void set_dropped_sensor(sensor::Sensor *s) { this->dropped_sensor_ = s; }

  /// Auth integration -------------------------------------------------------

  /// Register the handler for server commands (single consumer: auth).
  void set_command_handler(CommandHandler &&handler) { this->command_handler_ = std::move(handler); }

  /// Emit a token event asking the server to decide (no `status` field) and
  /// flush immediately; any commands in the response are dispatched before
  /// this returns. @return false if delivery failed (caller applies its
  /// offline policy).
  bool send_token_ask(const std::string &auth_device, const std::string &token);

  /// Emit a token event recording a local decision or a detach.
  void queue_token_event(const std::string &auth_device, const std::string &token, bool attached,
                         kbcore::TokenStatus status, const std::string &user);

  bool is_paired() const { return !this->bearer_token_.empty(); }

 protected:
  struct MeterState {
    kegboard_meter::KegboardMeter *meter;
    uint32_t last_update_ms{0};
    std::string last_update_pour_id;
  };

  kbcore::Event make_event_(const char *type, std::string data_json);
  void enqueue_(kbcore::Event &&event, bool reset_backoff = true);
  std::string rfc3339_now_();

  void enqueue_status_(bool boot);
  void collect_pour_updates_(std::vector<kbcore::Event> &out);

  /// Send one batch (queued + ephemeral). Returns true on 2xx.
  bool send_batch_(std::vector<kbcore::Event> &&ephemeral);
  void handle_response_(int status, const std::string &body, size_t queued_in_batch);
  void dispatch_commands_(const std::string &body);
  void handle_pairing_(const std::string &body);

  /// Deliveries are currently succeeding. Deliberately not conditioned on
  /// is_paired(): against a server that never asks for auth, the device runs
  /// unpaired forever and must still behave fully (protocol §2).
  bool healthy_() const { return !this->denied_ && this->consecutive_failures_ == 0; }
  void bump_backoff_();
  void publish_diagnostics_();
  void load_token_();
  void save_token_(const std::string &token);

  kegboard::KegboardHub *hub_{nullptr};
  http_request::HttpRequestComponent *http_{nullptr};
  time::RealTimeClock *time_{nullptr};

  std::string reporting_url_;
  std::string bearer_token_;
  std::string boot_id_;
  uint32_t next_event_id_{1};

  kbcore::RingQueue<kbcore::Event, kbcore::MAX_BATCH_EVENTS> queue_;
  /// Extra drops beyond the queue's own count (e.g. 4xx-dropped batches).
  uint32_t extra_dropped_{0};

  std::vector<MeterState> meters_;
  CommandHandler command_handler_;

  uint32_t heartbeat_ms_{60000};
  uint32_t pour_update_ms_{1000};
  uint32_t retry_interval_ms_{30000};

  uint32_t next_attempt_ms_{0};
  uint32_t next_heartbeat_ms_{0};
  uint32_t consecutive_failures_{0};

  /// Pairing state. denied_ stops all polling until reboot.
  bool denied_{false};
  uint32_t pairing_started_ms_{0};

  /// Recently applied command ids; the server re-sends until acked, so a
  /// command may arrive more than once and must be applied only once.
  std::vector<std::string> recent_command_ids_;

  ESPPreferenceObject token_pref_;

  sensor::Sensor *queue_depth_sensor_{nullptr};
  sensor::Sensor *dropped_sensor_{nullptr};
  uint32_t last_published_depth_{UINT32_MAX};
  uint32_t last_published_dropped_{UINT32_MAX};
};

}  // namespace esphome::kegboard_reporter
