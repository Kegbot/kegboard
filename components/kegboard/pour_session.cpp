#include "pour_session.h"

namespace kbcore {

bool PourSession::add_ticks(uint32_t ticks, uint32_t now_ms, uint32_t now_unix) {
  if (ticks == 0)
    return false;

  this->total_ticks_ += ticks;

  bool started = false;
  if (!this->pouring_) {
    this->pouring_ = true;
    this->session_ticks_ = 0;
    this->start_ms_ = now_ms;
    this->start_unix_ = now_unix;
    this->series_.reset(this->series_resolution_ms_);
    started = true;
  }

  this->session_ticks_ += ticks;
  this->last_tick_ms_ = now_ms;
  this->series_.add(now_ms - this->start_ms_, ticks);

  return started;
}

bool PourSession::poll(uint32_t now_ms, PourRecord *out) {
  if (!this->pouring_)
    return false;

  // Unsigned subtraction here is deliberate: it stays correct across the
  // 32-bit millisecond rollover at ~49.7 days of uptime.
  bool idle = (now_ms - this->last_tick_ms_) >= this->config_.idle_timeout_ms;
  bool too_long = this->config_.max_duration_ms != 0 && (now_ms - this->start_ms_) >= this->config_.max_duration_ms;

  if (!idle && !too_long)
    return false;

  return this->finish_(now_ms, out);
}

bool PourSession::end_now(uint32_t now_ms, PourRecord *out) {
  if (!this->pouring_)
    return false;
  return this->finish_(now_ms, out);
}

bool PourSession::finish_(uint32_t now_ms, PourRecord *out) {
  (void) now_ms;

  uint32_t ticks = this->session_ticks_;

  this->pouring_ = false;
  this->session_ticks_ = 0;

  // Below the threshold this was a drip, a bump, or line noise. Drop it
  // rather than reporting a pour nobody made.
  if (ticks < this->config_.min_pour_ticks) {
    this->series_.reset(this->series_resolution_ms_);
    return false;
  }

  if (out != nullptr) {
    out->ticks = ticks;
    out->volume_ml = ticks * this->config_.ml_per_tick;
    out->start_unix = this->start_unix_;
    // Measure to the last tick, not to the moment we noticed the pour ended,
    // so the idle timeout is not counted as pour duration.
    out->duration_ms = this->last_tick_ms_ - this->start_ms_;
    out->series = this->series_;
  }

  this->series_.reset(this->series_resolution_ms_);
  return true;
}

}  // namespace kbcore
