#include "grant_table.h"

namespace kbcore {

void GrantTable::authorize(const std::vector<uint8_t> &meters, const std::string &user, const std::string &auth_device,
                           const std::string &token, uint32_t duration_ms, uint32_t now_ms) {
  if (duration_ms > this->max_duration_ms_)
    duration_ms = this->max_duration_ms_;

  for (uint8_t meter : meters) {
    Grant grant;
    grant.user = user;
    grant.auth_device = auth_device;
    grant.token = token;
    grant.duration_ms = duration_ms;
    grant.expires_at_ms = now_ms + duration_ms;

    bool replaced = false;
    for (auto &entry : this->entries_) {
      if (entry.meter == meter) {
        entry.grant = grant;
        replaced = true;
        break;
      }
    }
    if (!replaced)
      this->entries_.push_back(Entry{meter, grant});
  }
}

std::vector<uint8_t> GrantTable::deauthorize(const std::vector<uint8_t> &meters) {
  std::vector<uint8_t> revoked;
  for (uint8_t meter : meters) {
    for (auto it = this->entries_.begin(); it != this->entries_.end(); ++it) {
      if (it->meter == meter) {
        revoked.push_back(meter);
        this->entries_.erase(it);
        break;
      }
    }
  }
  return revoked;
}

std::vector<uint8_t> GrantTable::deauthorize_all() {
  std::vector<uint8_t> revoked;
  revoked.reserve(this->entries_.size());
  for (const auto &entry : this->entries_)
    revoked.push_back(entry.meter);
  this->entries_.clear();
  return revoked;
}

std::vector<uint8_t> GrantTable::detach(const std::string &token) {
  std::vector<uint8_t> revoked;
  for (auto it = this->entries_.begin(); it != this->entries_.end();) {
    if (it->grant.token == token) {
      revoked.push_back(it->meter);
      it = this->entries_.erase(it);
    } else {
      ++it;
    }
  }
  return revoked;
}

void GrantTable::extend(uint8_t meter, uint32_t now_ms) {
  for (auto &entry : this->entries_) {
    if (entry.meter == meter) {
      entry.grant.expires_at_ms = now_ms + entry.grant.duration_ms;
      return;
    }
  }
}

std::vector<uint8_t> GrantTable::poll(uint32_t now_ms) {
  std::vector<uint8_t> expired;
  for (auto it = this->entries_.begin(); it != this->entries_.end();) {
    // Signed difference survives the 32-bit millisecond rollover; an
    // unsigned comparison would expire every grant at the wrap.
    if (static_cast<int32_t>(now_ms - it->grant.expires_at_ms) >= 0) {
      expired.push_back(it->meter);
      it = this->entries_.erase(it);
    } else {
      ++it;
    }
  }
  return expired;
}

const Grant *GrantTable::grant_for(uint8_t meter) const {
  for (const auto &entry : this->entries_) {
    if (entry.meter == meter)
      return &entry.grant;
  }
  return nullptr;
}

}  // namespace kbcore
