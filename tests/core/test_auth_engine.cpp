#include "auth_engine.h"

#include <map>
#include <set>

#include "test_support.h"

using kbcore::AuthDevice;
using kbcore::AuthEngine;
using kbcore::GrantEndReason;
using kbcore::GrantSpec;

namespace {

/// A fake board: two meters (0, 1), two relays (0, 1). Records every device
/// action in order, and mirrors the real pour path: end_pour() finishes the
/// in-flight pour and feeds its volume back through engine->on_pour_end,
/// exactly as the meter's pour callback does on hardware.
struct FakeDevice {
  std::map<uint8_t, bool> pouring;
  std::map<uint8_t, float> session_ml;
  std::map<uint8_t, bool> relay_state;
  std::map<uint8_t, std::string> attribution;  // meter -> grant_id ("" = guest)
  std::vector<std::string> log;
  std::vector<kbcore::GrantEnd> ends;
  AuthEngine *engine{nullptr};
  uint32_t now{0};
  /// Extra volume "in flight" between the engine's settle and the pour's
  /// final record — emulates ticks landing in that window.
  float end_pour_extra_ml{0.0f};

  AuthDevice callbacks() {
    AuthDevice d;
    d.has_meter = [](uint8_t m) { return m <= 1; };
    d.has_relay = [](uint8_t r) { return r <= 1; };
    d.is_pouring = [this](uint8_t m) { return this->pouring[m]; };
    d.session_volume_ml = [this](uint8_t m) { return this->session_ml[m]; };
    d.end_pour = [this](uint8_t m) {
      this->log.push_back("end_pour m" + std::to_string(m));
      if (!this->pouring[m])
        return;
      this->pouring[m] = false;
      const float final_ml = this->session_ml[m] + this->end_pour_extra_ml;
      this->session_ml[m] = 0.0f;
      if (this->engine != nullptr)
        this->engine->on_pour_end(m, final_ml, this->now);
    };
    d.set_attribution = [this](uint8_t m, const GrantSpec &spec) {
      this->log.push_back("attr m" + std::to_string(m) + "=" + spec.grant_id);
      this->attribution[m] = spec.grant_id;
    };
    d.clear_attribution = [this](uint8_t m) {
      this->log.push_back("clear m" + std::to_string(m));
      this->attribution[m] = "";
    };
    d.set_relay = [this](uint8_t r, bool on) {
      this->log.push_back(std::string(on ? "relay_on r" : "relay_off r") + std::to_string(r));
      this->relay_state[r] = on;
    };
    d.emit_grant_end = [this](const kbcore::GrantEnd &end) {
      this->log.push_back("grant_end " + end.grant_id + " " + std::string(kbcore::grant_end_reason_str(end.reason)));
      this->ends.push_back(end);
    };
    return d;
  }

  size_t log_index(const std::string &entry) const {
    for (size_t i = 0; i < log.size(); i++) {
      if (log[i] == entry)
        return i;
    }
    return SIZE_MAX;
  }
};

GrantSpec spec(const std::string &id, std::vector<uint8_t> meters, std::vector<uint8_t> relays = {}) {
  GrantSpec s;
  s.grant_id = id;
  s.meters = std::move(meters);
  s.relays = std::move(relays);
  s.auth_device = "core.rfid";
  s.token = "0089f2c4";
  return s;
}

struct Rig {
  FakeDevice dev;
  AuthEngine engine;
  Rig() : engine(dev.callbacks()) { dev.engine = &engine; }
};

TEST(authorize_applies_attribution_and_relays) {
  Rig rig;
  const auto outcome = rig.engine.authorize(spec("g_1", {0}, {1}), 1000);

  CHECK(outcome.ok);
  CHECK_FALSE(outcome.updated);
  CHECK_EQ(rig.dev.attribution[0], std::string("g_1"));
  CHECK(rig.dev.relay_state[1]);
  CHECK(rig.engine.any_active());
}

TEST(unknown_meter_or_relay_rejected_in_whole) {
  Rig rig;
  CHECK_FALSE(rig.engine.authorize(spec("g_1", {7}), 1000).ok);
  CHECK_FALSE(rig.engine.authorize(spec("g_2", {0}, {9}), 1000).ok);
  CHECK_FALSE(rig.engine.authorize(spec("", {0}), 1000).ok);
  CHECK_FALSE(rig.engine.authorize(spec("g_3", {}), 1000).ok);

  // Nothing was applied: no actions, no grants.
  CHECK(rig.dev.log.empty());
  CHECK_FALSE(rig.engine.any_active());
}

TEST(replacement_splits_the_pour_before_the_grant_end) {
  Rig rig;
  rig.engine.authorize(spec("g_alice", {0}, {0}), 1000);
  rig.dev.pouring[0] = true;
  rig.dev.session_ml[0] = 120.0f;
  rig.dev.now = 5000;

  rig.engine.authorize(spec("g_bob", {0}, {0}), 5000);

  // Order: Alice's pour ends, then her grant_end, then Bob's attribution.
  const size_t end_pour = rig.dev.log_index("end_pour m0");
  const size_t grant_end = rig.dev.log_index("grant_end g_alice replaced");
  const size_t attr_bob = rig.dev.log_index("attr m0=g_bob");
  CHECK(end_pour < grant_end);
  CHECK(grant_end < attr_bob);
  // The shared relay stays on for Bob.
  CHECK(rig.dev.relay_state[0]);
  CHECK_EQ(rig.engine.grants().grant_for(0)->spec.grant_id, std::string("g_bob"));
}

TEST(adoption_attributes_but_does_not_charge_pre_grant_volume) {
  Rig rig;
  // A guest pour is already running when the grant arrives.
  rig.dev.pouring[0] = true;
  rig.dev.session_ml[0] = 200.0f;

  auto s = spec("g_1", {0});
  s.max_volume_ml = 150.0f;
  rig.engine.authorize(s, 1000);
  CHECK_EQ(rig.dev.attribution[0], std::string("g_1"));

  // Pre-grant volume is excluded: 200 mL already poured does not trip the
  // 150 mL limit, and neither does 100 mL more...
  rig.engine.poll(2000);
  CHECK(rig.engine.any_active());
  rig.dev.session_ml[0] = 300.0f;
  rig.engine.poll(2500);
  CHECK(rig.engine.any_active());

  // ...but 160 mL of post-grant flow does.
  rig.dev.session_ml[0] = 360.0f;
  rig.dev.now = 3000;
  rig.engine.poll(3000);
  CHECK_FALSE(rig.engine.any_active());
  CHECK_EQ(rig.dev.ends.size(), 1u);
  CHECK(rig.dev.ends[0].reason == GrantEndReason::MAX_VOLUME);
}

TEST(volume_limit_ends_pour_before_grant_end) {
  Rig rig;
  auto s = spec("g_1", {0}, {0});
  s.max_volume_ml = 100.0f;
  rig.engine.authorize(s, 1000);

  rig.dev.pouring[0] = true;
  rig.dev.session_ml[0] = 60.0f;
  rig.engine.poll(2000);
  CHECK(rig.engine.any_active());

  rig.dev.session_ml[0] = 110.0f;
  rig.dev.now = 3000;
  rig.engine.poll(3000);

  const size_t end_pour = rig.dev.log_index("end_pour m0");
  const size_t grant_end = rig.dev.log_index("grant_end g_1 max_volume");
  CHECK(end_pour != SIZE_MAX);
  CHECK(end_pour < grant_end);
  CHECK_FALSE(rig.dev.relay_state[0]);
  CHECK_EQ(rig.dev.attribution[0], std::string(""));
}

TEST(pour_end_true_up_does_not_double_count) {
  Rig rig;
  rig.engine.authorize(spec("g_1", {0}), 1000);
  rig.dev.pouring[0] = true;

  rig.dev.session_ml[0] = 60.0f;
  rig.engine.poll(2000);  // 60 mL observed live

  // The pour ends naturally at 65 mL: only the 5 mL tail is new.
  rig.dev.pouring[0] = false;
  rig.engine.on_pour_end(0, 65.0f, 3000);
  CHECK_EQ(static_cast<int>(rig.engine.grants().grant_by_id("g_1")->poured_ml), 65);
}

TEST(idle_limit_expires_between_pours) {
  Rig rig;
  auto s = spec("g_1", {0});
  s.max_idle_ms = 10000;
  rig.engine.authorize(s, 1000);

  rig.engine.poll(10999);
  CHECK(rig.engine.any_active());
  rig.engine.poll(11000);
  CHECK_FALSE(rig.engine.any_active());
  CHECK(rig.dev.ends[0].reason == GrantEndReason::MAX_IDLE);
}

TEST(update_swaps_relays_and_keeps_counters) {
  Rig rig;
  rig.engine.authorize(spec("g_1", {0}, {0}), 1000);
  rig.dev.pouring[0] = true;
  rig.dev.session_ml[0] = 40.0f;
  rig.engine.poll(2000);  // 40 mL on the books

  const auto outcome = rig.engine.authorize(spec("g_1", {0}, {1}), 3000);
  CHECK(outcome.ok);
  CHECK(outcome.updated);
  CHECK_FALSE(rig.dev.relay_state[0]);  // dropped by the update
  CHECK(rig.dev.relay_state[1]);
  CHECK_EQ(static_cast<int>(rig.engine.grants().grant_by_id("g_1")->poured_ml), 40);
  // An update is not an ending.
  CHECK(rig.dev.ends.empty());
}

TEST(shared_relay_released_by_the_last_grant) {
  Rig rig;
  rig.engine.authorize(spec("g_1", {0}, {1}), 1000);
  rig.engine.authorize(spec("g_2", {1}, {1}), 1000);

  rig.engine.deauthorize({"g_1"}, false, 2000);
  CHECK(rig.dev.relay_state[1]);  // g_2 still names it

  rig.engine.deauthorize({"g_2"}, false, 3000);
  CHECK_FALSE(rig.dev.relay_state[1]);
}

TEST(deauthorize_all_and_unknown_ids) {
  Rig rig;
  rig.engine.authorize(spec("g_1", {0}), 1000);
  rig.engine.authorize(spec("g_2", {1}), 1000);

  CHECK_EQ(rig.engine.deauthorize({"g_missing"}, false, 2000), 0u);
  CHECK_EQ(rig.engine.deauthorize({}, true, 3000), 2u);
  CHECK_FALSE(rig.engine.any_active());
}

TEST(detach_ends_the_presented_tokens_grants) {
  Rig rig;
  rig.engine.authorize(spec("g_1", {0}), 1000);
  CHECK_EQ(rig.engine.detach("onewire", "0089f2c4", 2000), 0u);  // wrong reader
  CHECK_EQ(rig.engine.detach("core.rfid", "0089f2c4", 2000), 1u);
  CHECK(rig.dev.ends[0].reason == GrantEndReason::DETACH);
}

TEST(grant_killed_while_being_applied_gets_no_relays) {
  Rig rig;
  // Alice pours under g_alice; ticks keep landing between the engine's
  // settle and the pour's final record.
  rig.engine.authorize(spec("g_alice", {0}), 1000);
  rig.dev.pouring[0] = true;
  rig.dev.session_ml[0] = 50.0f;
  rig.engine.poll(2000);
  rig.dev.end_pour_extra_ml = 20.0f;
  rig.dev.now = 3000;

  // Bob's replacement grant has a tiny volume budget; Alice's 20 mL tail is
  // charged to it (flow is credited to whoever covers the meter when it is
  // observed) and kills it during application.
  auto bob = spec("g_bob", {0}, {1});
  bob.max_volume_ml = 10.0f;
  const auto outcome = rig.engine.authorize(bob, 3000);

  CHECK(outcome.ok);
  CHECK_FALSE(outcome.message.empty());  // "ended while being applied"
  CHECK_FALSE(rig.engine.any_active());
  // The dead grant's relay was never energized, and its ending was emitted.
  CHECK_FALSE(rig.dev.relay_state[1]);
  CHECK_EQ(rig.dev.log_index("relay_on r1"), SIZE_MAX);
  CHECK(rig.dev.log_index("grant_end g_bob max_volume") != SIZE_MAX);
}

TEST(guest_flow_after_grant_end_touches_nothing) {
  Rig rig;
  auto s = spec("g_1", {0});
  s.max_duration_ms = 5000;
  rig.engine.authorize(s, 1000);
  rig.engine.poll(6000);
  CHECK_FALSE(rig.engine.any_active());

  // The tap keeps flowing (no valve): nobody is charged, nothing happens.
  rig.dev.pouring[0] = true;
  rig.dev.session_ml[0] = 500.0f;
  rig.engine.poll(7000);
  rig.engine.on_pour_end(0, 500.0f, 8000);
  CHECK_EQ(rig.dev.ends.size(), 1u);
}

}  // namespace

TEST_MAIN("auth_engine", {
  RUN(authorize_applies_attribution_and_relays);
  RUN(unknown_meter_or_relay_rejected_in_whole);
  RUN(replacement_splits_the_pour_before_the_grant_end);
  RUN(adoption_attributes_but_does_not_charge_pre_grant_volume);
  RUN(volume_limit_ends_pour_before_grant_end);
  RUN(pour_end_true_up_does_not_double_count);
  RUN(idle_limit_expires_between_pours);
  RUN(update_swaps_relays_and_keeps_counters);
  RUN(shared_relay_released_by_the_last_grant);
  RUN(deauthorize_all_and_unknown_ids);
  RUN(detach_ends_the_presented_tokens_grants);
  RUN(grant_killed_while_being_applied_gets_no_relays);
  RUN(guest_flow_after_grant_end_touches_nothing);
})
