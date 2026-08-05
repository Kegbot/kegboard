#pragma once

// Delivery scheduling for the event protocol (docs/kegboard-event-protocol.md):
// the status-code table, command dedup and re-acks, pairing cadence, and
// retry backoff. The reporter feeds observations in; this answers "send
// now?" and "what was that command's result?". Keeping the policy here is
// what lets the host suite pin the protocol's promises down without an
// HTTP stack.
//
// Part of the framework-agnostic kegboard core: no ESPHome, Arduino, or
// ESP-IDF headers. See CORE.md.

#include <cstdint>
#include <string>
#include <vector>

namespace kbcore {

/// What the protocol's status-code table tells the device to do with a
/// batch. Network errors and timeouts never reach classify_status: they
/// are TRANSIENT by definition.
enum class BatchDisposition : uint8_t {
  ACCEPTED,   ///< 2xx: dequeue the batch, process the response body.
  PAIRING,    ///< 401: keep events queued, enter/continue pairing.
  REJECTED,   ///< other 4xx: the batch can never succeed; drop it.
  TRANSIENT,  ///< 5xx: keep events queued, back off.
};

BatchDisposition classify_status(int http_status);

/// Attempt timing, backoff, pairing cadence, and the command ledger.
/// All timestamps are the device's monotonic milliseconds; comparisons are
/// rollover-safe (32-bit signed differences).
class Delivery {
 public:
  static constexpr uint32_t MAX_RETRY_INTERVAL_MS = 300000;
  static constexpr uint32_t PAIRING_FAST_POLL_MS = 5000;
  static constexpr uint32_t PAIRING_FAST_WINDOW_MS = 60000;
  static constexpr size_t COMMAND_DEDUP_WINDOW = 16;

  void set_retry_interval_ms(uint32_t v) { this->retry_interval_ms_ = v; }
  void set_heartbeat_ms(uint32_t v) { this->heartbeat_ms_ = v; }

  /// Pairing was denied: nothing more is sent until reboot.
  bool denied() const { return this->denied_; }

  /// Whether a send attempt is allowed now.
  bool due(uint32_t now_ms) const {
    return !this->denied_ && static_cast<int32_t>(now_ms - this->next_attempt_ms_) >= 0;
  }

  uint32_t consecutive_failures() const { return this->consecutive_failures_; }

  /// An event was queued. Pours and tokens reset backoff and earn an
  /// immediate attempt; everything else waits its turn.
  void note_enqueue(bool reset_backoff, uint32_t now_ms);

  /// 2xx: batch delivered.
  void on_accepted(uint32_t now_ms);
  /// Other 4xx: batch dropped; the next batch may go immediately.
  void on_rejected(uint32_t now_ms);
  /// 5xx / network error / timeout. @return the delay applied, for logging.
  uint32_t on_transient(uint32_t now_ms);

  /// Entering (or re-entering) pairing anchors the fast-poll window.
  void pairing_started(uint32_t now_ms) { this->pairing_started_ms_ = now_ms; }
  /// 401 + pending: poll fast inside the window, then at heartbeat cadence.
  void on_pairing_pending(uint32_t now_ms);
  /// 401 + allowed: deliver the queued backlog immediately.
  void on_pairing_allowed(uint32_t now_ms);
  void on_pairing_denied() { this->denied_ = true; }

  /// The recorded result for a command id, or nullptr if unseen. A
  /// duplicate is re-acknowledged with this, never re-applied.
  const char *command_result(const std::string &id) const;
  void record_command(const std::string &id, const char *result);

 private:
  struct AppliedCommand {
    std::string id;
    const char *result;
  };

  uint32_t retry_interval_ms_{30000};
  uint32_t heartbeat_ms_{60000};
  uint32_t next_attempt_ms_{0};
  uint32_t consecutive_failures_{0};
  uint32_t pairing_started_ms_{0};
  bool denied_{false};
  std::vector<AppliedCommand> recent_commands_;
};

}  // namespace kbcore
