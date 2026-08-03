#include "auth_session.h"

namespace kbcore {

bool AuthSession::attach(const std::string &device, const std::string &token, uint32_t now_ms) {
  const bool is_new = !this->authorized_ || this->token_ != token;

  this->authorized_ = true;
  this->device_ = device;
  this->token_ = token;
  this->expires_at_ms_ = now_ms + this->config_.grant_duration_ms;

  // A different token means a different person; do not let them inherit the
  // previous holder's identity.
  if (is_new)
    this->username_.clear();

  return is_new;
}

bool AuthSession::detach(const std::string &token) {
  if (!this->authorized_ || this->token_ != token)
    return false;
  this->clear_();
  return true;
}

bool AuthSession::revoke() {
  if (!this->authorized_)
    return false;
  this->clear_();
  return true;
}

void AuthSession::extend(uint32_t now_ms) {
  if (!this->authorized_)
    return;
  this->expires_at_ms_ = now_ms + this->config_.grant_duration_ms;
}

bool AuthSession::poll(uint32_t now_ms) {
  if (!this->authorized_)
    return false;

  // Signed difference so the comparison survives the 32-bit millisecond
  // rollover at ~49.7 days of uptime; an unsigned test would expire every
  // active grant at the wrap.
  if (static_cast<int32_t>(now_ms - this->expires_at_ms_) < 0)
    return false;

  this->clear_();
  return true;
}

uint32_t AuthSession::remaining_ms(uint32_t now_ms) const {
  if (!this->authorized_)
    return 0;
  const int32_t remaining = static_cast<int32_t>(this->expires_at_ms_ - now_ms);
  return remaining > 0 ? static_cast<uint32_t>(remaining) : 0;
}

void AuthSession::clear_() {
  this->authorized_ = false;
  this->device_.clear();
  this->token_.clear();
  this->username_.clear();
  this->expires_at_ms_ = 0;
}

}  // namespace kbcore
