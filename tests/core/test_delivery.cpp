#include "delivery.h"

#include "test_support.h"

using kbcore::BatchDisposition;
using kbcore::classify_status;
using kbcore::Delivery;

namespace {

TEST(status_table_matches_the_protocol) {
  CHECK(classify_status(200) == BatchDisposition::ACCEPTED);
  CHECK(classify_status(204) == BatchDisposition::ACCEPTED);
  CHECK(classify_status(299) == BatchDisposition::ACCEPTED);
  CHECK(classify_status(401) == BatchDisposition::PAIRING);
  CHECK(classify_status(400) == BatchDisposition::REJECTED);
  CHECK(classify_status(404) == BatchDisposition::REJECTED);
  CHECK(classify_status(422) == BatchDisposition::REJECTED);
  CHECK(classify_status(500) == BatchDisposition::TRANSIENT);
  CHECK(classify_status(503) == BatchDisposition::TRANSIENT);
  CHECK(classify_status(302) == BatchDisposition::TRANSIENT);
}

TEST(starts_due_and_not_denied) {
  Delivery d;
  CHECK(d.due(0));
  CHECK_FALSE(d.denied());
  CHECK_EQ(d.consecutive_failures(), 0u);
}

TEST(backoff_doubles_and_caps) {
  Delivery d;
  d.set_retry_interval_ms(30000);

  CHECK_EQ(d.on_transient(1000), 30000u);
  CHECK_FALSE(d.due(1000 + 29999));
  CHECK(d.due(1000 + 30000));

  CHECK_EQ(d.on_transient(1000), 60000u);
  CHECK_EQ(d.on_transient(1000), 120000u);
  CHECK_EQ(d.on_transient(1000), 240000u);
  // The doubling caps at five minutes.
  CHECK_EQ(d.on_transient(1000), 300000u);
  CHECK_EQ(d.on_transient(1000), 300000u);
  CHECK_EQ(d.consecutive_failures(), 6u);
}

TEST(success_resets_backoff) {
  Delivery d;
  d.on_transient(1000);
  d.on_transient(1000);

  d.on_accepted(5000);
  CHECK_EQ(d.consecutive_failures(), 0u);
  CHECK(d.due(5000));
  // The next failure starts from the base interval again.
  CHECK_EQ(d.on_transient(5000), 30000u);
}

TEST(rejected_batch_does_not_back_off) {
  Delivery d;
  d.on_transient(1000);  // deep in backoff
  d.on_rejected(2000);   // 4xx: batch dropped, next batch may go now
  CHECK(d.due(2000));
  // ...but the failure count survives: the connection is not "healthy".
  CHECK_EQ(d.consecutive_failures(), 1u);
}

TEST(only_flagged_enqueues_reset_backoff) {
  Delivery d;
  d.on_transient(1000);  // next attempt at 31000

  d.note_enqueue(false, 2000);  // temperature/status/ack: waits its turn
  CHECK_FALSE(d.due(2000));

  d.note_enqueue(true, 2000);  // pour/token: immediate attempt
  CHECK(d.due(2000));
}

TEST(pairing_polls_fast_then_at_heartbeat_cadence) {
  Delivery d;
  d.set_heartbeat_ms(60000);
  d.pairing_started(0);

  d.on_pairing_pending(1000);  // inside the fast window
  CHECK_FALSE(d.due(1000 + 4999));
  CHECK(d.due(1000 + 5000));

  d.on_pairing_pending(70000);  // window over: heartbeat cadence
  CHECK_FALSE(d.due(70000 + 59999));
  CHECK(d.due(70000 + 60000));

  // Re-entering pairing (e.g. a revoked token) re-opens the fast window.
  d.pairing_started(500000);
  d.on_pairing_pending(500000);
  CHECK(d.due(500000 + 5000));
}

TEST(pairing_allowed_sends_backlog_immediately) {
  Delivery d;
  d.on_pairing_pending(1000);
  d.on_pairing_allowed(2000);
  CHECK(d.due(2000));
}

TEST(pairing_denied_stops_everything) {
  Delivery d;
  d.on_pairing_denied();
  CHECK(d.denied());
  CHECK_FALSE(d.due(1000000));
}

TEST(due_survives_millis_rollover) {
  Delivery d;
  CHECK_EQ(d.on_transient(0xFFFFF000u), 30000u);
  // ~4 s later, wrapped through zero: not due yet.
  CHECK_FALSE(d.due(100));
  CHECK(d.due(100 + 30000));
}

TEST(command_ledger_records_and_evicts) {
  Delivery d;
  CHECK(d.command_result("cmd_1") == nullptr);

  d.record_command("cmd_1", "ok");
  d.record_command("cmd_2", "error");
  CHECK_EQ(std::string(d.command_result("cmd_1")), std::string("ok"));
  CHECK_EQ(std::string(d.command_result("cmd_2")), std::string("error"));

  // The window is bounded; the oldest entries fall out.
  for (int i = 0; i < static_cast<int>(Delivery::COMMAND_DEDUP_WINDOW); i++)
    d.record_command("cmd_fill_" + std::to_string(i), "ok");
  CHECK(d.command_result("cmd_1") == nullptr);
  CHECK(d.command_result("cmd_fill_5") != nullptr);
}

}  // namespace

TEST_MAIN("delivery", {
  RUN(status_table_matches_the_protocol);
  RUN(starts_due_and_not_denied);
  RUN(backoff_doubles_and_caps);
  RUN(success_resets_backoff);
  RUN(rejected_batch_does_not_back_off);
  RUN(only_flagged_enqueues_reset_backoff);
  RUN(pairing_polls_fast_then_at_heartbeat_cadence);
  RUN(pairing_allowed_sends_backlog_immediately);
  RUN(pairing_denied_stops_everything);
  RUN(due_survives_millis_rollover);
  RUN(command_ledger_records_and_evicts);
})
