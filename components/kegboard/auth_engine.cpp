#include "auth_engine.h"

#include <algorithm>

namespace kbcore {

static bool contains_u8(const std::vector<uint8_t> &v, uint8_t value) {
  return std::find(v.begin(), v.end(), value) != v.end();
}

void AuthEngine::adopt_in_flight_pour_(uint8_t meter) {
  // POLICY — a grant arriving mid-pour ADOPTS the pour: it keeps running
  // and, at its end, is attributed to the grant in full — the
  // forgot-to-authenticate-first case. Limit
  // accounting is not retroactive: the baseline below excludes the volume
  // already poured. To change the policy to "split into a new pour"
  // instead, call device_.end_pour here — everything else stays as is.
  if (this->device_.is_pouring(meter))
    this->pour_seen_ml_[meter] = this->device_.session_volume_ml(meter);
}

AuthEngine::Outcome AuthEngine::authorize(const GrantSpec &spec, uint32_t now_ms) {
  if (spec.grant_id.empty())
    return {false, false, "missing grant_id"};
  if (spec.meters.empty())
    return {false, false, "missing meter_numbers"};
  // A grant naming a meter or relay the device does not have is rejected
  // in whole.
  for (uint8_t meter : spec.meters) {
    if (!this->device_.has_meter(meter))
      return {false, false, "unknown meter " + std::to_string(meter)};
  }
  for (uint8_t relay : spec.relays) {
    if (!this->device_.has_relay(relay))
      return {false, false, "unknown relay " + std::to_string(relay)};
  }

  // Copy before authorize() mutates the table; relays an update drops must
  // release below (unless another grant still names them).
  const Grant *existing = this->grants_.grant_by_id(spec.grant_id);
  const bool updated = existing != nullptr;
  const std::vector<uint8_t> prev_relays = updated ? existing->spec.relays : std::vector<uint8_t>{};

  this->process_ends_(this->grants_.authorize(spec, now_ms));

  for (uint8_t relay : prev_relays) {
    if (!contains_u8(spec.relays, relay) && !this->grants_.covers_relay(relay))
      this->device_.set_relay(relay, false);
  }

  // Reentrancy: process_ends_ ends replaced grants' pours, whose pour path
  // feeds flow back through on_pour_end — which can, in principle, end the
  // just-created grant (a tail delta against a tiny max_volume_ml). Its
  // grant_end is emitted already; do not energize relays for a dead grant.
  if (this->grants_.grant_by_id(spec.grant_id) == nullptr)
    return {true, updated, "grant ended while being applied"};

  for (uint8_t meter : spec.meters) {
    this->device_.set_attribution(meter, spec);
    this->adopt_in_flight_pour_(meter);
  }
  for (uint8_t relay : spec.relays)
    this->device_.set_relay(relay, true);

  return {true, updated, ""};
}

size_t AuthEngine::deauthorize(const std::vector<std::string> &grant_ids, bool all, uint32_t now_ms) {
  // Ids matching no active grant are ignored; the grant may have already
  // ended on its own, and the grant_end stream is the record.
  const auto ends = all ? this->grants_.deauthorize_all(now_ms) : this->grants_.deauthorize(grant_ids, now_ms);
  const size_t count = ends.size();
  this->process_ends_(ends);
  return count;
}

size_t AuthEngine::detach(const std::string &auth_device, const std::string &token, uint32_t now_ms) {
  const auto ends = this->grants_.detach(auth_device, token, now_ms);
  const size_t count = ends.size();
  this->process_ends_(ends);
  return count;
}

void AuthEngine::on_pour_end(uint8_t meter, float volume_ml, uint32_t now_ms) {
  float seen = 0.0f;
  auto it = this->pour_seen_ml_.find(meter);
  if (it != this->pour_seen_ml_.end()) {
    seen = it->second;
    it->second = 0.0f;
  }
  const float delta = volume_ml - seen;
  if (delta > 0.0f)
    this->process_ends_(this->grants_.record_flow(meter, delta, now_ms));
}

void AuthEngine::poll(uint32_t now_ms) {
  if (!this->grants_.any_active()) {
    if (!this->pour_seen_ml_.empty())
      this->pour_seen_ml_.clear();
    return;
  }

  // A pour that ended without settling its counter (e.g. a discarded drip)
  // must not poison the next pour's deltas.
  for (auto &entry : this->pour_seen_ml_) {
    if (!this->device_.is_pouring(entry.first))
      entry.second = 0.0f;
  }

  // Live flow accounting: deltas reset the idle clock and accrue toward
  // max_volume_ml, so the valve closes the moment a limit trips, not at
  // pour end.
  for (uint8_t meter : this->grants_.active_meters()) {
    if (!this->device_.is_pouring(meter))
      continue;
    float &seen = this->pour_seen_ml_[meter];
    const float volume = this->device_.session_volume_ml(meter);
    if (volume > seen) {
      const float delta = volume - seen;
      seen = volume;
      this->process_ends_(this->grants_.record_flow(meter, delta, now_ms));
    }
  }

  this->process_ends_(this->grants_.poll(now_ms));
}

void AuthEngine::process_ends_(const std::vector<GrantEnd> &ends) {
  if (ends.empty())
    return;
  for (const auto &end : ends) {
    for (uint8_t meter : end.meters) {
      // Settle the flow-accounting baseline first: the departing grant's
      // totals are already snapshotted, and the pour's last unobserved
      // dribble must not be credited to whichever grant covers this meter
      // next.
      if (this->device_.is_pouring(meter))
        this->pour_seen_ml_[meter] = this->device_.session_volume_ml(meter);
      // End any pour in flight before clearing attribution, so the drink
      // still lands on the departing grant — and its pour event precedes
      // the grant_end.
      this->device_.end_pour(meter);
      this->device_.clear_attribution(meter);
    }
    for (uint8_t relay : end.relays) {
      // A relay stays energized while any other active grant names it.
      if (!this->grants_.covers_relay(relay))
        this->device_.set_relay(relay, false);
    }
    this->device_.emit_grant_end(end);
  }
}

}  // namespace kbcore
