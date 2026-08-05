#include "delivery.h"

namespace kbcore {

BatchDisposition classify_status(int http_status) {
  if (http_status >= 200 && http_status < 300)
    return BatchDisposition::ACCEPTED;
  if (http_status == 401)
    return BatchDisposition::PAIRING;
  if (http_status >= 400 && http_status < 500)
    return BatchDisposition::REJECTED;
  return BatchDisposition::TRANSIENT;
}

void Delivery::note_enqueue(bool reset_backoff, uint32_t now_ms) {
  if (reset_backoff)
    this->next_attempt_ms_ = now_ms;
}

void Delivery::on_accepted(uint32_t now_ms) {
  this->consecutive_failures_ = 0;
  this->next_attempt_ms_ = now_ms;
}

void Delivery::on_rejected(uint32_t now_ms) { this->next_attempt_ms_ = now_ms; }

uint32_t Delivery::on_transient(uint32_t now_ms) {
  this->consecutive_failures_++;
  uint32_t delay_ms = this->retry_interval_ms_;
  for (uint32_t i = 1; i < this->consecutive_failures_ && delay_ms < MAX_RETRY_INTERVAL_MS; i++)
    delay_ms *= 2;
  if (delay_ms > MAX_RETRY_INTERVAL_MS)
    delay_ms = MAX_RETRY_INTERVAL_MS;
  this->next_attempt_ms_ = now_ms + delay_ms;
  return delay_ms;
}

void Delivery::on_pairing_pending(uint32_t now_ms) {
  const uint32_t since_start = now_ms - this->pairing_started_ms_;
  const uint32_t interval = since_start < PAIRING_FAST_WINDOW_MS ? PAIRING_FAST_POLL_MS : this->heartbeat_ms_;
  this->next_attempt_ms_ = now_ms + interval;
}

void Delivery::on_pairing_allowed(uint32_t now_ms) { this->next_attempt_ms_ = now_ms; }

const char *Delivery::command_result(const std::string &id) const {
  for (const auto &recent : this->recent_commands_) {
    if (recent.id == id)
      return recent.result;
  }
  return nullptr;
}

void Delivery::record_command(const std::string &id, const char *result) {
  this->recent_commands_.push_back(AppliedCommand{id, result});
  if (this->recent_commands_.size() > COMMAND_DEDUP_WINDOW)
    this->recent_commands_.erase(this->recent_commands_.begin());
}

}  // namespace kbcore
