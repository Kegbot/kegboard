#include "auth_session.h"

#include "test_support.h"

using kbcore::AuthConfig;
using kbcore::AuthSession;

namespace {

AuthConfig default_config() {
  AuthConfig c;
  c.grant_duration_ms = 30000;
  return c;
}

TEST(starts_unauthorized) {
  AuthSession s(default_config());
  CHECK_FALSE(s.is_authorized());
  CHECK_EQ(s.username(), std::string(""));
  CHECK_EQ(s.remaining_ms(0), 0u);
}

TEST(attach_grants_access) {
  AuthSession s(default_config());

  CHECK(s.attach("core.rfid", "deadbeef", 1000));
  CHECK(s.is_authorized());
  CHECK_EQ(s.device(), std::string("core.rfid"));
  CHECK_EQ(s.token(), std::string("deadbeef"));
  CHECK_EQ(s.remaining_ms(1000), 30000u);
}

TEST(reattaching_same_token_refreshes_without_reporting_new) {
  AuthSession s(default_config());

  CHECK(s.attach("core.rfid", "deadbeef", 1000));
  CHECK_FALSE(s.attach("core.rfid", "deadbeef", 20000));
  CHECK(s.is_authorized());
  CHECK_EQ(s.remaining_ms(20000), 30000u);
}

TEST(refreshing_preserves_resolved_username) {
  AuthSession s(default_config());

  s.attach("core.rfid", "deadbeef", 1000);
  s.set_username("mikey");
  s.attach("core.rfid", "deadbeef", 20000);

  CHECK_EQ(s.username(), std::string("mikey"));
}

TEST(different_token_replaces_grant_and_clears_username) {
  // The new arrival must not inherit the previous holder's identity, or
  // their pour lands on someone else's tab.
  AuthSession s(default_config());

  s.attach("core.rfid", "deadbeef", 1000);
  s.set_username("mikey");

  CHECK(s.attach("core.rfid", "cafe1234", 2000));
  CHECK_EQ(s.token(), std::string("cafe1234"));
  CHECK_EQ(s.username(), std::string(""));
}

TEST(grant_expires_after_duration) {
  AuthSession s(default_config());
  s.attach("core.rfid", "deadbeef", 1000);

  CHECK_FALSE(s.poll(30000));
  CHECK(s.is_authorized());

  CHECK(s.poll(31000));
  CHECK_FALSE(s.is_authorized());

  // Expiry is reported once, not on every subsequent poll.
  CHECK_FALSE(s.poll(32000));
}

TEST(detach_revokes_matching_token) {
  AuthSession s(default_config());
  s.attach("onewire", "0000000012345678", 1000);

  CHECK(s.detach("0000000012345678"));
  CHECK_FALSE(s.is_authorized());
}

TEST(detach_of_other_token_is_ignored) {
  // A stale event from a second reader must not close the tap on whoever
  // currently holds the grant.
  AuthSession s(default_config());
  s.attach("onewire", "aaaa", 1000);

  CHECK_FALSE(s.detach("bbbb"));
  CHECK(s.is_authorized());
  CHECK_EQ(s.token(), std::string("aaaa"));
}

TEST(detach_when_unauthorized_is_a_no_op) {
  AuthSession s(default_config());
  CHECK_FALSE(s.detach("aaaa"));
}

TEST(revoke_clears_any_grant) {
  AuthSession s(default_config());
  s.attach("core.rfid", "deadbeef", 1000);
  s.set_username("mikey");

  CHECK(s.revoke());
  CHECK_FALSE(s.is_authorized());
  CHECK_EQ(s.username(), std::string(""));
  CHECK_FALSE(s.revoke());
}

TEST(extend_pushes_out_expiry) {
  // A pour longer than the grant window must not be cut off mid-glass.
  AuthSession s(default_config());
  s.attach("core.rfid", "deadbeef", 1000);

  s.extend(25000);
  CHECK_FALSE(s.poll(31000));
  CHECK(s.is_authorized());
  CHECK(s.poll(56000));
}

TEST(extend_when_unauthorized_does_not_grant) {
  AuthSession s(default_config());
  s.extend(1000);
  CHECK_FALSE(s.is_authorized());
  CHECK_EQ(s.remaining_ms(1000), 0u);
}

TEST(remaining_time_counts_down_and_floors_at_zero) {
  AuthSession s(default_config());
  s.attach("core.rfid", "deadbeef", 1000);

  CHECK_EQ(s.remaining_ms(11000), 20000u);
  CHECK_EQ(s.remaining_ms(31000), 0u);
  CHECK_EQ(s.remaining_ms(99000), 0u);
}

TEST(millis_rollover_does_not_expire_grant_early) {
  // At ~49.7 days the millisecond counter wraps. A grant issued just before
  // the wrap must survive it.
  AuthSession s(default_config());

  const uint32_t before_wrap = 0xFFFFF000u;
  s.attach("core.rfid", "deadbeef", before_wrap);

  // ~4 s later, having wrapped through zero.
  const uint32_t after_wrap = 100;
  CHECK_FALSE(s.poll(after_wrap));
  CHECK(s.is_authorized());

  // ...and still expires on schedule afterwards.
  CHECK(s.poll(after_wrap + 30000));
}

TEST(zero_duration_expires_immediately) {
  AuthConfig c;
  c.grant_duration_ms = 0;
  AuthSession s(c);

  s.attach("core.rfid", "deadbeef", 1000);
  CHECK(s.poll(1000));
  CHECK_FALSE(s.is_authorized());
}

TEST(grant_duration_change_applies_to_next_attach) {
  AuthSession s(default_config());
  s.set_grant_duration_ms(5000);
  s.attach("core.rfid", "deadbeef", 1000);

  CHECK_EQ(s.remaining_ms(1000), 5000u);
  CHECK(s.poll(6000));
}

}  // namespace

TEST_MAIN("auth_session", {
  RUN(starts_unauthorized);
  RUN(attach_grants_access);
  RUN(reattaching_same_token_refreshes_without_reporting_new);
  RUN(refreshing_preserves_resolved_username);
  RUN(different_token_replaces_grant_and_clears_username);
  RUN(grant_expires_after_duration);
  RUN(detach_revokes_matching_token);
  RUN(detach_of_other_token_is_ignored);
  RUN(detach_when_unauthorized_is_a_no_op);
  RUN(revoke_clears_any_grant);
  RUN(extend_pushes_out_expiry);
  RUN(extend_when_unauthorized_does_not_grant);
  RUN(remaining_time_counts_down_and_floors_at_zero);
  RUN(millis_rollover_does_not_expire_grant_early);
  RUN(zero_duration_expires_immediately);
  RUN(grant_duration_change_applies_to_next_attach);
})
