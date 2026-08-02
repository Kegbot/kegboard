#include "pour_session.h"

#include "test_support.h"

using kegboard::PourConfig;
using kegboard::PourRecord;
using kegboard::PourSession;

namespace {

PourConfig default_config() {
  PourConfig c;
  c.idle_timeout_ms = 10000;
  c.min_pour_ticks = 3;
  c.max_duration_ms = 300000;
  c.ml_per_tick = 0.5f;
  return c;
}

TEST(idle_until_first_tick) {
  PourSession s(default_config());
  PourRecord rec;

  CHECK_FALSE(s.is_pouring());
  CHECK_FALSE(s.poll(1000, &rec));
  CHECK_EQ(s.total_ticks(), 0u);
}

TEST(first_tick_starts_pour) {
  PourSession s(default_config());

  CHECK(s.add_ticks(1, 1000, 1700000000));
  CHECK(s.is_pouring());
  CHECK_EQ(s.session_ticks(), 1u);

  // A subsequent tick continues the same pour rather than starting a new one.
  CHECK_FALSE(s.add_ticks(1, 1100, 1700000000));
  CHECK_EQ(s.session_ticks(), 2u);
}

TEST(pour_ends_after_idle_timeout) {
  PourSession s(default_config());
  PourRecord rec;

  s.add_ticks(10, 1000, 1700000000);
  s.add_ticks(10, 2000, 1700000000);

  // Still within the idle window.
  CHECK_FALSE(s.poll(11000, &rec));
  CHECK(s.is_pouring());

  CHECK(s.poll(12000, &rec));
  CHECK_FALSE(s.is_pouring());
  CHECK_EQ(rec.ticks, 20u);
  CHECK_NEAR(rec.volume_ml, 10.0f, 0.0001f);
}

TEST(duration_measures_to_last_tick_not_to_timeout) {
  // The idle timeout must not inflate the reported pour duration; a 1s pour
  // followed by 10s of silence is a 1s pour.
  PourSession s(default_config());
  PourRecord rec;

  s.add_ticks(5, 1000, 1700000000);
  s.add_ticks(5, 2000, 1700000000);
  CHECK(s.poll(12000, &rec));

  CHECK_EQ(rec.duration_ms, 1000u);
}

TEST(short_pour_is_discarded) {
  // Two ticks is below min_pour_ticks: a drip, not a pour.
  PourSession s(default_config());
  PourRecord rec;

  s.add_ticks(2, 1000, 1700000000);
  CHECK_FALSE(s.poll(12000, &rec));
  CHECK_FALSE(s.is_pouring());

  // ...but it still counts toward the lifetime total, since the meter really
  // did move.
  CHECK_EQ(s.total_ticks(), 2u);
}

TEST(discarded_pour_does_not_leak_into_next_pour) {
  PourSession s(default_config());
  PourRecord rec;

  s.add_ticks(2, 1000, 1700000000);
  CHECK_FALSE(s.poll(12000, &rec));

  s.add_ticks(10, 20000, 1700000000);
  CHECK(s.poll(31000, &rec));
  CHECK_EQ(rec.ticks, 10u);
}

TEST(max_duration_force_ends_pour) {
  PourConfig c = default_config();
  c.max_duration_ms = 5000;
  PourSession s(c);
  PourRecord rec;

  s.add_ticks(10, 1000, 1700000000);
  // Keep ticking so the idle timeout never fires; only the cutoff can end it.
  for (uint32_t t = 2000; t <= 5000; t += 1000)
    s.add_ticks(10, t, 1700000000);

  CHECK(s.poll(6001, &rec));
  CHECK_EQ(rec.ticks, 50u);
}

TEST(max_duration_zero_disables_cutoff) {
  PourConfig c = default_config();
  c.max_duration_ms = 0;
  PourSession s(c);
  PourRecord rec;

  s.add_ticks(10, 1000, 1700000000);
  for (uint32_t t = 2000; t <= 600000; t += 1000)
    s.add_ticks(1, t, 1700000000);

  CHECK(s.is_pouring());
  CHECK_FALSE(s.poll(600500, &rec));
}

TEST(end_now_finishes_in_progress_pour) {
  PourSession s(default_config());
  PourRecord rec;

  s.add_ticks(10, 1000, 1700000000);
  CHECK(s.end_now(1500, &rec));
  CHECK_EQ(rec.ticks, 10u);
  CHECK_FALSE(s.is_pouring());

  // Nothing in progress; nothing to end.
  CHECK_FALSE(s.end_now(2000, &rec));
}

TEST(millis_rollover_does_not_end_pour_early) {
  // At ~49.7 days of uptime the millisecond counter wraps. Unsigned
  // subtraction keeps the elapsed-time comparisons correct across the wrap;
  // a signed or naive comparison would end every in-flight pour.
  PourSession s(default_config());
  PourRecord rec;

  uint32_t before_wrap = 0xFFFFFF00u;
  s.add_ticks(10, before_wrap, 1700000000);

  uint32_t after_wrap = 500;  // ~756 ms later, having wrapped through zero
  CHECK_FALSE(s.poll(after_wrap, &rec));
  CHECK(s.is_pouring());

  s.add_ticks(10, after_wrap, 1700000000);
  CHECK(s.poll(after_wrap + 11000, &rec));
  CHECK_EQ(rec.ticks, 20u);
}

TEST(unsynced_clock_yields_zero_start_unix) {
  PourSession s(default_config());
  PourRecord rec;

  s.add_ticks(10, 1000, 0);
  CHECK(s.poll(12000, &rec));
  CHECK_EQ(rec.start_unix, 0u);
}

TEST(start_unix_is_captured_at_pour_start) {
  // The clock may sync mid-pour; the record should reflect when the pour
  // began, not when it ended.
  PourSession s(default_config());
  PourRecord rec;

  s.add_ticks(10, 1000, 1700000000);
  s.add_ticks(10, 2000, 1700000099);
  CHECK(s.poll(13000, &rec));
  CHECK_EQ(rec.start_unix, 1700000000u);
}

TEST(total_ticks_survives_pours_and_resets_on_demand) {
  PourSession s(default_config());
  PourRecord rec;

  s.add_ticks(10, 1000, 0);
  s.poll(12000, &rec);
  s.add_ticks(10, 20000, 0);
  s.poll(31000, &rec);

  CHECK_EQ(s.total_ticks(), 20u);
  CHECK_EQ(s.session_ticks(), 0u);

  s.reset_total();
  CHECK_EQ(s.total_ticks(), 0u);
}

TEST(calibration_change_applies_to_next_report) {
  PourSession s(default_config());
  PourRecord rec;

  s.set_ml_per_tick(2.0f);
  s.add_ticks(10, 1000, 0);
  CHECK_NEAR(s.session_volume_ml(), 20.0f, 0.0001f);
  CHECK(s.poll(12000, &rec));
  CHECK_NEAR(rec.volume_ml, 20.0f, 0.0001f);
}

TEST(pour_carries_tick_series) {
  PourSession s(default_config());
  PourRecord rec;

  s.set_series_resolution_ms(100);
  s.add_ticks(3, 1000, 0);
  s.add_ticks(4, 1150, 0);
  CHECK(s.poll(12000, &rec));

  CHECK_EQ(rec.series.to_string(), std::string("0:3 100:4"));
}

TEST(zero_ticks_is_a_no_op) {
  PourSession s(default_config());

  CHECK_FALSE(s.add_ticks(0, 1000, 1700000000));
  CHECK_FALSE(s.is_pouring());
  CHECK_EQ(s.total_ticks(), 0u);
}

}  // namespace

TEST_MAIN("pour_session", {
  RUN(idle_until_first_tick);
  RUN(first_tick_starts_pour);
  RUN(pour_ends_after_idle_timeout);
  RUN(duration_measures_to_last_tick_not_to_timeout);
  RUN(short_pour_is_discarded);
  RUN(discarded_pour_does_not_leak_into_next_pour);
  RUN(max_duration_force_ends_pour);
  RUN(max_duration_zero_disables_cutoff);
  RUN(end_now_finishes_in_progress_pour);
  RUN(millis_rollover_does_not_end_pour_early);
  RUN(unsynced_clock_yields_zero_start_unix);
  RUN(start_unix_is_captured_at_pour_start);
  RUN(total_ticks_survives_pours_and_resets_on_demand);
  RUN(calibration_change_applies_to_next_report);
  RUN(pour_carries_tick_series);
  RUN(zero_ticks_is_a_no_op);
})
