#include "grant_table.h"

#include "test_support.h"

using kbcore::GrantEndReason;
using kbcore::GrantSpec;
using kbcore::GrantTable;

namespace {

GrantSpec spec(const std::string &id, std::vector<uint8_t> meters, std::vector<uint8_t> relays = {}) {
  GrantSpec s;
  s.grant_id = id;
  s.meters = std::move(meters);
  s.relays = std::move(relays);
  s.auth_device = "core.rfid";
  s.token = "0089f2c4";
  return s;
}

TEST(starts_empty) {
  GrantTable t;
  CHECK_FALSE(t.any_active());
  CHECK(t.grant_for(0) == nullptr);
  CHECK(t.grant_by_id("g_1") == nullptr);
}

TEST(authorize_covers_each_meter) {
  GrantTable t;
  CHECK(t.authorize(spec("g_1", {0, 2}, {1}), 1000).empty());

  CHECK_EQ(t.active_count(), 1u);
  CHECK(t.grant_for(0) != nullptr);
  CHECK(t.grant_for(2) != nullptr);
  CHECK(t.grant_for(1) == nullptr);
  CHECK(t.covers_relay(1));
  CHECK_FALSE(t.covers_relay(0));
  CHECK_EQ(t.grant_by_id("g_1")->spec.token, std::string("0089f2c4"));
}

TEST(new_grant_takes_over_meters_with_replaced_ending) {
  GrantTable t;
  t.authorize(spec("g_alice", {0, 1}), 1000);

  const auto ends = t.authorize(spec("g_bob", {0}), 2000);
  CHECK_EQ(ends.size(), 1u);
  CHECK(ends[0].reason == GrantEndReason::REPLACED);
  CHECK_EQ(ends[0].grant_id, std::string("g_alice"));
  CHECK_EQ(ends[0].meters.size(), 1u);
  CHECK_EQ(ends[0].meters[0], 0);
  // Alice keeps meter 1; the partial ending does not release her relays.
  CHECK(ends[0].relays.empty());
  CHECK(t.grant_by_id("g_alice") != nullptr);
  CHECK_EQ(t.grant_for(0)->spec.grant_id, std::string("g_bob"));
  CHECK_EQ(t.grant_for(1)->spec.grant_id, std::string("g_alice"));
}

TEST(full_takeover_removes_grant_and_reports_relays) {
  GrantTable t;
  t.authorize(spec("g_alice", {0}, {3}), 1000);

  const auto ends = t.authorize(spec("g_bob", {0}), 2000);
  CHECK_EQ(ends.size(), 1u);
  CHECK_EQ(ends[0].relays.size(), 1u);
  CHECK_EQ(ends[0].relays[0], 3);
  CHECK(t.grant_by_id("g_alice") == nullptr);
  CHECK_FALSE(t.covers_relay(3));
}

TEST(update_in_place_keeps_counters) {
  GrantTable t;
  t.authorize(spec("g_1", {0}), 1000);
  t.record_flow(0, 100.0f, 2000);

  auto updated = spec("g_1", {0});
  updated.max_volume_ml = 500.0f;
  CHECK(t.authorize(updated, 3000).empty());

  CHECK_EQ(t.active_count(), 1u);
  const auto *g = t.grant_by_id("g_1");
  CHECK_EQ(static_cast<int>(g->poured_ml), 100);
  CHECK_EQ(g->created_ms, 1000u);  // age carries over
}

TEST(update_sheds_meters_with_replaced_ending) {
  GrantTable t;
  t.authorize(spec("g_1", {0, 1}), 1000);

  const auto ends = t.authorize(spec("g_1", {0}), 2000);
  CHECK_EQ(ends.size(), 1u);
  CHECK(ends[0].reason == GrantEndReason::REPLACED);
  CHECK_EQ(ends[0].meters[0], 1);
  CHECK(t.grant_for(1) == nullptr);
  CHECK(t.grant_for(0) != nullptr);
}

TEST(deauthorize_by_id_ignores_unknown) {
  GrantTable t;
  t.authorize(spec("g_1", {0}), 1000);
  t.authorize(spec("g_2", {1}), 1000);

  const auto ends = t.deauthorize({"g_1", "g_missing"}, 2000);
  CHECK_EQ(ends.size(), 1u);
  CHECK(ends[0].reason == GrantEndReason::COMMAND);
  CHECK_EQ(ends[0].grant_id, std::string("g_1"));
  CHECK(t.grant_by_id("g_2") != nullptr);
}

TEST(deauthorize_all_clears_everything) {
  GrantTable t;
  t.authorize(spec("g_1", {0}), 1000);
  t.authorize(spec("g_2", {1}), 1000);

  CHECK_EQ(t.deauthorize_all(2000).size(), 2u);
  CHECK_FALSE(t.any_active());
  CHECK(t.deauthorize_all(2000).empty());
}

TEST(detach_ends_only_that_tokens_grants) {
  GrantTable t;
  auto alice = spec("g_alice", {0, 1});
  alice.token = "aa";
  auto bob = spec("g_bob", {2});
  bob.token = "bb";
  t.authorize(alice, 1000);
  t.authorize(bob, 1000);

  const auto ends = t.detach("core.rfid", "aa", 5000);
  CHECK_EQ(ends.size(), 1u);
  CHECK(ends[0].reason == GrantEndReason::DETACH);
  CHECK_EQ(ends[0].meters.size(), 2u);
  CHECK_EQ(ends[0].duration_ms, 4000u);
  CHECK(t.grant_by_id("g_bob") != nullptr);
  CHECK(t.detach("core.rfid", "zz", 5000).empty());
}

TEST(detach_matches_the_auth_device_when_both_carry_one) {
  GrantTable t;
  t.authorize(spec("g_1", {0}), 1000);  // auth_device core.rfid, token 0089f2c4

  // Same token value leaving a different reader is someone else's tap.
  CHECK(t.detach("onewire", "0089f2c4", 2000).empty());
  CHECK_EQ(t.detach("core.rfid", "0089f2c4", 2000).size(), 1u);

  // A grant issued without a device echo matches on token alone.
  auto s = spec("g_2", {1});
  s.auth_device = "";
  t.authorize(s, 1000);
  CHECK_EQ(t.detach("onewire", "0089f2c4", 2000).size(), 1u);
}

TEST(volume_limit_trips_on_record_flow) {
  GrantTable t;
  auto s = spec("g_1", {0});
  s.max_volume_ml = 500.0f;
  t.authorize(s, 1000);

  CHECK(t.record_flow(0, 499.0f, 2000).empty());
  const auto ends = t.record_flow(0, 2.0f, 3000);
  CHECK_EQ(ends.size(), 1u);
  CHECK(ends[0].reason == GrantEndReason::MAX_VOLUME);
  CHECK_EQ(static_cast<int>(ends[0].volume_ml), 501);
  CHECK_FALSE(t.any_active());
}

TEST(volume_accrues_across_meters_of_one_grant) {
  GrantTable t;
  auto s = spec("g_1", {0, 1});
  s.max_volume_ml = 100.0f;
  t.authorize(s, 1000);

  CHECK(t.record_flow(0, 60.0f, 2000).empty());
  CHECK_EQ(t.record_flow(1, 60.0f, 3000).size(), 1u);
}

TEST(zero_max_volume_is_unlimited) {
  GrantTable t;
  t.authorize(spec("g_1", {0}), 1000);
  CHECK(t.record_flow(0, 1e6f, 2000).empty());
  CHECK(t.any_active());
}

TEST(duration_expires_on_poll) {
  GrantTable t;
  auto s = spec("g_1", {0});
  s.max_duration_ms = 30000;
  t.authorize(s, 1000);

  CHECK(t.poll(30999).empty());
  const auto ends = t.poll(31000);
  CHECK_EQ(ends.size(), 1u);
  CHECK(ends[0].reason == GrantEndReason::MAX_DURATION);
  CHECK_FALSE(t.any_active());
  CHECK(t.poll(32000).empty());
}

TEST(duration_is_clamped_to_table_max) {
  GrantTable t;
  t.set_max_duration_ms(300000);
  auto s = spec("g_1", {0});
  s.max_duration_ms = 86400000;
  t.authorize(s, 1000);

  CHECK_EQ(t.poll(301000).size(), 1u);
}

TEST(zero_duration_means_clamp_not_immediate) {
  GrantTable t;
  t.set_max_duration_ms(300000);
  t.authorize(spec("g_1", {0}), 1000);

  CHECK(t.poll(1000).empty());
  CHECK(t.poll(300999).empty());
  CHECK_EQ(t.poll(301000).size(), 1u);
}

TEST(update_cannot_extend_past_clamp) {
  GrantTable t;
  t.set_max_duration_ms(300000);
  t.authorize(spec("g_1", {0}), 1000);

  // A later update re-requests a long duration; the clamp still runs from
  // the original creation.
  auto s = spec("g_1", {0});
  s.max_duration_ms = 86400000;
  t.authorize(s, 200000);
  CHECK_EQ(t.poll(301000).size(), 1u);
}

TEST(idle_limit_expires_without_flow) {
  GrantTable t;
  auto s = spec("g_1", {0});
  s.max_idle_ms = 10000;
  t.authorize(s, 1000);

  CHECK(t.poll(10999).empty());
  t.record_flow(0, 5.0f, 9000);  // flow resets the idle clock
  CHECK(t.poll(18999).empty());
  const auto ends = t.poll(19000);
  CHECK_EQ(ends.size(), 1u);
  CHECK(ends[0].reason == GrantEndReason::MAX_IDLE);
}

TEST(expiry_survives_millis_rollover) {
  GrantTable t;
  auto s = spec("g_1", {0});
  s.max_duration_ms = 30000;
  t.authorize(s, 0xFFFFF000u);

  // ~4 s later, wrapped through zero: not yet expired.
  CHECK(t.poll(100).empty());
  CHECK(t.grant_for(0) != nullptr);
  CHECK_EQ(t.poll(100 + 30000).size(), 1u);
}

TEST(endings_snapshot_whole_grant_totals) {
  GrantTable t;
  t.authorize(spec("g_1", {0, 1}), 1000);
  t.record_flow(0, 100.0f, 2000);

  // A partial ending reports the grant's running totals, not a per-meter
  // delta (protocol §5.7).
  const auto ends = t.authorize(spec("g_2", {1}), 3000);
  CHECK_EQ(ends.size(), 1u);
  CHECK_EQ(static_cast<int>(ends[0].volume_ml), 100);
  CHECK_EQ(ends[0].duration_ms, 2000u);
}

TEST(record_flow_on_ungranted_meter_is_noop) {
  GrantTable t;
  t.authorize(spec("g_1", {0}), 1000);
  CHECK(t.record_flow(5, 100.0f, 2000).empty());
  CHECK_EQ(static_cast<int>(t.grant_by_id("g_1")->poured_ml), 0);
}

}  // namespace

TEST_MAIN("grant_table", {
  RUN(starts_empty);
  RUN(authorize_covers_each_meter);
  RUN(new_grant_takes_over_meters_with_replaced_ending);
  RUN(full_takeover_removes_grant_and_reports_relays);
  RUN(update_in_place_keeps_counters);
  RUN(update_sheds_meters_with_replaced_ending);
  RUN(deauthorize_by_id_ignores_unknown);
  RUN(deauthorize_all_clears_everything);
  RUN(detach_ends_only_that_tokens_grants);
  RUN(detach_matches_the_auth_device_when_both_carry_one);
  RUN(volume_limit_trips_on_record_flow);
  RUN(volume_accrues_across_meters_of_one_grant);
  RUN(zero_max_volume_is_unlimited);
  RUN(duration_expires_on_poll);
  RUN(duration_is_clamped_to_table_max);
  RUN(zero_duration_means_clamp_not_immediate);
  RUN(update_cannot_extend_past_clamp);
  RUN(idle_limit_expires_without_flow);
  RUN(expiry_survives_millis_rollover);
  RUN(endings_snapshot_whole_grant_totals);
  RUN(record_flow_on_ungranted_meter_is_noop);
})
