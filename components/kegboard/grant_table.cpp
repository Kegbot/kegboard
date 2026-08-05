#include "grant_table.h"

#include <algorithm>

namespace kbcore {

const char *grant_end_reason_str(GrantEndReason reason) {
  switch (reason) {
    case GrantEndReason::MAX_VOLUME:
      return "max_volume";
    case GrantEndReason::MAX_DURATION:
      return "max_duration";
    case GrantEndReason::MAX_IDLE:
      return "max_idle";
    case GrantEndReason::DETACH:
      return "detach";
    case GrantEndReason::COMMAND:
      return "command";
    case GrantEndReason::REPLACED:
      return "replaced";
  }
  return "command";
}

static bool contains_u8(const std::vector<uint8_t> &v, uint8_t value) {
  return std::find(v.begin(), v.end(), value) != v.end();
}

uint32_t GrantTable::effective_max_duration_(uint32_t requested_ms) const {
  if (requested_ms == 0 || requested_ms > this->max_duration_ms_)
    return this->max_duration_ms_;
  return requested_ms;
}

GrantEnd GrantTable::make_end_(const Grant &grant, std::vector<uint8_t> meters, GrantEndReason reason,
                               uint32_t now_ms) const {
  GrantEnd end;
  end.grant_id = grant.spec.grant_id;
  end.auth_device = grant.spec.auth_device;
  end.token = grant.spec.token;
  end.meters = std::move(meters);
  end.reason = reason;
  end.volume_ml = grant.poured_ml;
  end.duration_ms = now_ms - grant.created_ms;
  return end;
}

GrantEnd GrantTable::end_grant_(size_t index, GrantEndReason reason, uint32_t now_ms) {
  Grant &grant = this->grants_[index];
  GrantEnd end = this->make_end_(grant, grant.spec.meters, reason, now_ms);
  end.relays = grant.spec.relays;
  this->grants_.erase(this->grants_.begin() + index);
  return end;
}

std::vector<GrantEnd> GrantTable::authorize(const GrantSpec &spec, uint32_t now_ms) {
  std::vector<GrantEnd> ends;

  // Meters entering the new scope leave whichever other grant covers them;
  // an update also sheds the meters no longer in its scope.
  for (size_t i = 0; i < this->grants_.size();) {
    Grant &g = this->grants_[i];
    const bool is_target = g.spec.grant_id == spec.grant_id;
    std::vector<uint8_t> lost;
    for (uint8_t m : g.spec.meters) {
      const bool in_new_scope = contains_u8(spec.meters, m);
      if (is_target ? !in_new_scope : in_new_scope)
        lost.push_back(m);
    }
    if (lost.empty()) {
      i++;
      continue;
    }
    if (lost.size() == g.spec.meters.size() && !is_target) {
      ends.push_back(this->end_grant_(i, GrantEndReason::REPLACED, now_ms));
      continue;  // erased; this index now holds the next grant
    }
    auto &meters = g.spec.meters;
    meters.erase(std::remove_if(meters.begin(), meters.end(), [&](uint8_t m) { return contains_u8(lost, m); }),
                 meters.end());
    ends.push_back(this->make_end_(g, std::move(lost), GrantEndReason::REPLACED, now_ms));
    i++;
  }

  for (auto &g : this->grants_) {
    if (g.spec.grant_id == spec.grant_id) {
      // Update in place: sets and limits replace; counters carry over, so a
      // top-up cannot reset volume already poured, and the clamp still
      // bounds total lifetime from the original creation (protocol §7.1).
      g.spec = spec;
      g.effective_max_duration_ms = this->effective_max_duration_(spec.max_duration_ms);
      return ends;
    }
  }

  Grant g;
  g.spec = spec;
  g.created_ms = now_ms;
  g.last_flow_ms = now_ms;
  g.effective_max_duration_ms = this->effective_max_duration_(spec.max_duration_ms);
  this->grants_.push_back(std::move(g));
  return ends;
}

std::vector<GrantEnd> GrantTable::deauthorize(const std::vector<std::string> &grant_ids, uint32_t now_ms) {
  std::vector<GrantEnd> ends;
  for (size_t i = 0; i < this->grants_.size();) {
    const auto &id = this->grants_[i].spec.grant_id;
    if (std::find(grant_ids.begin(), grant_ids.end(), id) != grant_ids.end()) {
      ends.push_back(this->end_grant_(i, GrantEndReason::COMMAND, now_ms));
    } else {
      i++;
    }
  }
  return ends;
}

std::vector<GrantEnd> GrantTable::deauthorize_all(uint32_t now_ms) {
  std::vector<GrantEnd> ends;
  while (!this->grants_.empty())
    ends.push_back(this->end_grant_(0, GrantEndReason::COMMAND, now_ms));
  return ends;
}

std::vector<GrantEnd> GrantTable::detach(const std::string &auth_device, const std::string &token, uint32_t now_ms) {
  std::vector<GrantEnd> ends;
  for (size_t i = 0; i < this->grants_.size();) {
    const GrantSpec &spec = this->grants_[i].spec;
    const bool device_matches = spec.auth_device.empty() || auth_device.empty() || spec.auth_device == auth_device;
    if (spec.token == token && device_matches) {
      ends.push_back(this->end_grant_(i, GrantEndReason::DETACH, now_ms));
    } else {
      i++;
    }
  }
  return ends;
}

std::vector<GrantEnd> GrantTable::record_flow(uint8_t meter, float delta_ml, uint32_t now_ms) {
  for (size_t i = 0; i < this->grants_.size(); i++) {
    Grant &g = this->grants_[i];
    if (!contains_u8(g.spec.meters, meter))
      continue;
    g.last_flow_ms = now_ms;
    g.poured_ml += delta_ml;
    if (g.spec.max_volume_ml > 0.0f && g.poured_ml >= g.spec.max_volume_ml)
      return {this->end_grant_(i, GrantEndReason::MAX_VOLUME, now_ms)};
    return {};
  }
  return {};
}

std::vector<GrantEnd> GrantTable::poll(uint32_t now_ms) {
  std::vector<GrantEnd> ends;
  for (size_t i = 0; i < this->grants_.size();) {
    const Grant &g = this->grants_[i];
    // Signed differences survive the 32-bit millisecond rollover; unsigned
    // comparisons would expire every grant at the wrap.
    if (static_cast<int32_t>(now_ms - (g.created_ms + g.effective_max_duration_ms)) >= 0) {
      ends.push_back(this->end_grant_(i, GrantEndReason::MAX_DURATION, now_ms));
      continue;
    }
    if (g.spec.max_idle_ms > 0 && static_cast<int32_t>(now_ms - (g.last_flow_ms + g.spec.max_idle_ms)) >= 0) {
      ends.push_back(this->end_grant_(i, GrantEndReason::MAX_IDLE, now_ms));
      continue;
    }
    i++;
  }
  return ends;
}

const Grant *GrantTable::grant_for(uint8_t meter) const {
  for (const auto &g : this->grants_) {
    if (contains_u8(g.spec.meters, meter))
      return &g;
  }
  return nullptr;
}

const Grant *GrantTable::grant_by_id(const std::string &grant_id) const {
  for (const auto &g : this->grants_) {
    if (g.spec.grant_id == grant_id)
      return &g;
  }
  return nullptr;
}

bool GrantTable::covers_relay(uint8_t relay) const {
  for (const auto &g : this->grants_) {
    if (contains_u8(g.spec.relays, relay))
      return true;
  }
  return false;
}

std::vector<uint8_t> GrantTable::active_meters() const {
  std::vector<uint8_t> meters;
  for (const auto &g : this->grants_) {
    for (uint8_t m : g.spec.meters) {
      if (!contains_u8(meters, m))
        meters.push_back(m);
    }
  }
  return meters;
}

}  // namespace kbcore
