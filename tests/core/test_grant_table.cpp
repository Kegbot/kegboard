#include "grant_table.h"

#include "test_support.h"

using kbcore::GrantTable;

namespace {

TEST(starts_empty) {
  GrantTable t;
  CHECK_FALSE(t.any_active());
  CHECK(t.grant_for(0) == nullptr);
}

TEST(authorize_grants_each_listed_meter) {
  GrantTable t;
  t.authorize({0, 2}, "mikey", "core.rfid", "0089f2c4", 30000, 1000);

  CHECK_EQ(t.active_count(), 2u);
  CHECK(t.grant_for(0) != nullptr);
  CHECK(t.grant_for(2) != nullptr);
  CHECK(t.grant_for(1) == nullptr);
  CHECK_EQ(t.grant_for(0)->user, std::string("mikey"));
}

TEST(duration_is_clamped_to_max) {
  GrantTable t;
  t.set_max_duration_ms(300000);
  t.authorize({0}, "mikey", "core.rfid", "aa", 86400000, 1000);

  CHECK_EQ(t.grant_for(0)->duration_ms, 300000u);
  CHECK_EQ(t.grant_for(0)->expires_at_ms, 301000u);
}

TEST(new_grant_replaces_existing_on_same_meter) {
  GrantTable t;
  t.authorize({0}, "alice", "core.rfid", "aa", 30000, 1000);
  t.authorize({0}, "bob", "core.rfid", "bb", 30000, 2000);

  CHECK_EQ(t.active_count(), 1u);
  CHECK_EQ(t.grant_for(0)->user, std::string("bob"));
  CHECK_EQ(t.grant_for(0)->expires_at_ms, 32000u);
}

TEST(meters_are_independent) {
  GrantTable t;
  t.authorize({0}, "alice", "core.rfid", "aa", 30000, 1000);
  t.authorize({1}, "bob", "onewire", "bb", 30000, 1000);

  CHECK_EQ(t.grant_for(0)->user, std::string("alice"));
  CHECK_EQ(t.grant_for(1)->user, std::string("bob"));
}

TEST(deauthorize_revokes_only_listed_meters) {
  GrantTable t;
  t.authorize({0, 1, 2}, "mikey", "core.rfid", "aa", 30000, 1000);

  auto revoked = t.deauthorize({0, 2, 5});
  CHECK_EQ(revoked.size(), 2u);
  CHECK(t.grant_for(0) == nullptr);
  CHECK(t.grant_for(1) != nullptr);
  CHECK(t.grant_for(2) == nullptr);
}

TEST(deauthorize_all_clears_everything) {
  GrantTable t;
  t.authorize({0, 1}, "mikey", "core.rfid", "aa", 30000, 1000);

  auto revoked = t.deauthorize_all();
  CHECK_EQ(revoked.size(), 2u);
  CHECK_FALSE(t.any_active());

  CHECK(t.deauthorize_all().empty());
}

TEST(detach_revokes_only_that_tokens_grants) {
  // Alice on meters 0+1, Bob on meter 2; Alice's iButton leaves.
  GrantTable t;
  t.authorize({0, 1}, "alice", "onewire", "aa", 30000, 1000);
  t.authorize({2}, "bob", "onewire", "bb", 30000, 1000);

  auto revoked = t.detach("aa");
  CHECK_EQ(revoked.size(), 2u);
  CHECK(t.grant_for(0) == nullptr);
  CHECK(t.grant_for(2) != nullptr);

  CHECK(t.detach("zz").empty());
}

TEST(grants_expire_on_poll) {
  GrantTable t;
  t.authorize({0}, "mikey", "core.rfid", "aa", 30000, 1000);

  CHECK(t.poll(30999).empty());
  auto expired = t.poll(31000);
  CHECK_EQ(expired.size(), 1u);
  CHECK_FALSE(t.any_active());

  // Expiry reports once.
  CHECK(t.poll(32000).empty());
}

TEST(extend_renews_by_the_grants_own_duration) {
  GrantTable t;
  t.set_max_duration_ms(300000);
  t.authorize({0}, "mikey", "core.rfid", "aa", 30000, 1000);

  t.extend(0, 25000);
  CHECK(t.poll(31000).empty());
  CHECK_EQ(t.grant_for(0)->expires_at_ms, 55000u);

  // Extending an ungranted meter is a no-op, not a grant.
  t.extend(5, 25000);
  CHECK(t.grant_for(5) == nullptr);
}

TEST(expiry_survives_millis_rollover) {
  GrantTable t;
  t.authorize({0}, "mikey", "core.rfid", "aa", 30000, 0xFFFFF000u);

  // ~4 s later, wrapped through zero: not yet expired.
  CHECK(t.poll(100).empty());
  CHECK(t.grant_for(0) != nullptr);

  CHECK_EQ(t.poll(100 + 30000).size(), 1u);
}

TEST(zero_duration_expires_immediately) {
  GrantTable t;
  t.authorize({0}, "", "core.rfid", "aa", 0, 1000);
  CHECK_EQ(t.poll(1000).size(), 1u);
}

}  // namespace

TEST_MAIN("grant_table", {
  RUN(starts_empty);
  RUN(authorize_grants_each_listed_meter);
  RUN(duration_is_clamped_to_max);
  RUN(new_grant_replaces_existing_on_same_meter);
  RUN(meters_are_independent);
  RUN(deauthorize_revokes_only_listed_meters);
  RUN(deauthorize_all_clears_everything);
  RUN(detach_revokes_only_that_tokens_grants);
  RUN(grants_expire_on_poll);
  RUN(extend_renews_by_the_grants_own_duration);
  RUN(expiry_survives_millis_rollover);
  RUN(zero_duration_expires_immediately);
})
