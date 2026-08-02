#include "tick_series.h"

#include "test_support.h"

using kbcore::TickSeries;

namespace {

TEST(empty_series_serializes_to_empty_string) {
  TickSeries s;
  s.reset(100);
  CHECK(s.empty());
  CHECK_EQ(s.to_string(), std::string(""));
}

TEST(ticks_bucket_by_resolution) {
  TickSeries s;
  s.reset(100);
  s.add(0, 1);
  s.add(50, 2);   // same 0-99 bucket
  s.add(100, 4);  // next bucket
  s.add(199, 1);  // same 100-199 bucket

  CHECK_EQ(s.size(), 2u);
  CHECK_EQ(s.to_string(), std::string("0:3 100:5"));
}

TEST(zero_ticks_are_ignored) {
  TickSeries s;
  s.reset(100);
  s.add(0, 0);
  CHECK(s.empty());
}

TEST(zero_resolution_disables_recording) {
  TickSeries s;
  s.reset(0);
  CHECK_FALSE(s.enabled());
  s.add(0, 5);
  s.add(100, 5);
  CHECK(s.empty());
  CHECK_EQ(s.to_string(), std::string(""));
}

TEST(reset_clears_previous_series) {
  TickSeries s;
  s.reset(100);
  s.add(0, 5);
  s.reset(100);
  CHECK(s.empty());
  CHECK_FALSE(s.coarsened());
}

TEST(overflow_coarsens_instead_of_truncating) {
  // Fill well past capacity and confirm the series stays bounded, keeps every
  // tick, and reports that it lost time resolution.
  TickSeries s;
  s.reset(100);

  uint32_t expected_total = 0;
  for (uint32_t i = 0; i < TickSeries::CAPACITY * 4; i++) {
    s.add(i * 100, 1);
    expected_total += 1;
  }

  CHECK(s.size() <= TickSeries::CAPACITY);
  CHECK(s.coarsened());
  CHECK(s.resolution_ms() > 100u);

  uint32_t total = 0;
  for (size_t i = 0; i < s.size(); i++)
    total += s[i].ticks;
  CHECK_EQ(total, expected_total);
}

TEST(coarsened_offsets_stay_unique_and_increasing) {
  TickSeries s;
  s.reset(100);
  for (uint32_t i = 0; i < TickSeries::CAPACITY * 4; i++)
    s.add(i * 100, 1);

  for (size_t i = 1; i < s.size(); i++)
    CHECK(s[i].offset_ms > s[i - 1].offset_ms);
}

TEST(coarsened_offsets_align_to_resolution) {
  TickSeries s;
  s.reset(100);
  for (uint32_t i = 0; i < TickSeries::CAPACITY * 4; i++)
    s.add(i * 100, 1);

  for (size_t i = 0; i < s.size(); i++)
    CHECK_EQ(s[i].offset_ms % s.resolution_ms(), 0u);
}

TEST(serialization_matches_kegbot_wire_format) {
  // Kegbot's time_series.from_string() expects space-separated <time>:<amount>
  // pairs with non-negative integer times.
  TickSeries s;
  s.reset(250);
  s.add(0, 7);
  s.add(250, 3);
  s.add(750, 11);

  CHECK_EQ(s.to_string(), std::string("0:7 250:3 750:11"));
}

TEST(sparse_pour_leaves_gaps_rather_than_empty_buckets) {
  // A pour with a pause should not emit zero-tick buckets for the gap.
  TickSeries s;
  s.reset(100);
  s.add(0, 5);
  s.add(5000, 5);

  CHECK_EQ(s.size(), 2u);
  CHECK_EQ(s.to_string(), std::string("0:5 5000:5"));
}

}  // namespace

TEST_MAIN("tick_series", {
  RUN(empty_series_serializes_to_empty_string);
  RUN(ticks_bucket_by_resolution);
  RUN(zero_ticks_are_ignored);
  RUN(zero_resolution_disables_recording);
  RUN(reset_clears_previous_series);
  RUN(overflow_coarsens_instead_of_truncating);
  RUN(coarsened_offsets_stay_unique_and_increasing);
  RUN(coarsened_offsets_align_to_resolution);
  RUN(serialization_matches_kegbot_wire_format);
  RUN(sparse_pour_leaves_gaps_rather_than_empty_buckets);
})
