#include "ring_queue.h"

#include "test_support.h"

using kbcore::RingQueue;

namespace {

TEST(new_queue_is_empty) {
  RingQueue<int, 4> q;
  CHECK(q.empty());
  CHECK_FALSE(q.full());
  CHECK_EQ(q.size(), 0u);
  CHECK_EQ(q.dropped(), 0u);
  CHECK(q.peek() == nullptr);
}

TEST(pop_on_empty_queue_reports_failure) {
  RingQueue<int, 4> q;
  int out = 99;
  CHECK_FALSE(q.pop(&out));
  CHECK_EQ(out, 99);
}

TEST(items_come_out_in_order) {
  RingQueue<int, 4> q;
  q.push(1);
  q.push(2);
  q.push(3);

  int out = 0;
  CHECK(q.pop(&out));
  CHECK_EQ(out, 1);
  CHECK(q.pop(&out));
  CHECK_EQ(out, 2);
  CHECK(q.pop(&out));
  CHECK_EQ(out, 3);
  CHECK(q.empty());
}

TEST(peek_returns_oldest_without_removing) {
  RingQueue<int, 4> q;
  q.push(7);
  q.push(8);

  CHECK_EQ(*q.peek(), 7);
  CHECK_EQ(*q.peek(), 7);
  CHECK_EQ(q.size(), 2u);
}

TEST(push_reports_success_until_full) {
  RingQueue<int, 3> q;
  CHECK(q.push(1));
  CHECK(q.push(2));
  CHECK(q.push(3));
  CHECK(q.full());
}

TEST(overflow_evicts_oldest_and_counts_the_loss) {
  RingQueue<int, 3> q;
  q.push(1);
  q.push(2);
  q.push(3);

  CHECK_FALSE(q.push(4));
  CHECK_EQ(q.dropped(), 1u);
  CHECK_EQ(q.size(), 3u);

  // 1 was the casualty; 2, 3, 4 survive in order.
  int out = 0;
  q.pop(&out);
  CHECK_EQ(out, 2);
  q.pop(&out);
  CHECK_EQ(out, 3);
  q.pop(&out);
  CHECK_EQ(out, 4);
}

TEST(wraparound_preserves_order_over_many_cycles) {
  RingQueue<int, 4> q;
  for (int i = 0; i < 100; i++) {
    q.push(i);
    int out = 0;
    CHECK(q.pop(&out));
    CHECK_EQ(out, i);
  }
  CHECK(q.empty());
  CHECK_EQ(q.dropped(), 0u);
}

TEST(interleaved_push_and_pop_stay_ordered) {
  RingQueue<int, 4> q;
  q.push(1);
  q.push(2);

  int out = 0;
  q.pop(&out);
  CHECK_EQ(out, 1);

  q.push(3);
  q.push(4);
  q.push(5);
  CHECK(q.full());
  CHECK_EQ(q.dropped(), 0u);

  q.pop(&out);
  CHECK_EQ(out, 2);
  q.pop(&out);
  CHECK_EQ(out, 3);
}

TEST(clear_empties_the_queue) {
  RingQueue<int, 4> q;
  q.push(1);
  q.push(2);
  q.clear();

  CHECK(q.empty());
  CHECK(q.peek() == nullptr);
}

TEST(sustained_overflow_keeps_the_newest_entries) {
  // Models a long server outage: the queue must stay bounded and retain the
  // most recent pours rather than an ancient prefix.
  RingQueue<int, 8> q;
  for (int i = 0; i < 1000; i++)
    q.push(i);

  CHECK_EQ(q.size(), 8u);
  CHECK_EQ(q.dropped(), 992u);
  CHECK_EQ(*q.peek(), 992);
}

TEST(works_with_non_trivial_element_types) {
  RingQueue<std::string, 2> q;
  q.push("first");
  q.push("second");
  CHECK_FALSE(q.push("third"));

  std::string out;
  q.pop(&out);
  CHECK_EQ(out, std::string("second"));
}

}  // namespace

TEST_MAIN("ring_queue", {
  RUN(new_queue_is_empty);
  RUN(pop_on_empty_queue_reports_failure);
  RUN(items_come_out_in_order);
  RUN(peek_returns_oldest_without_removing);
  RUN(push_reports_success_until_full);
  RUN(overflow_evicts_oldest_and_counts_the_loss);
  RUN(wraparound_preserves_order_over_many_cycles);
  RUN(interleaved_push_and_pop_stay_ordered);
  RUN(clear_empties_the_queue);
  RUN(sustained_overflow_keeps_the_newest_entries);
  RUN(works_with_non_trivial_element_types);
})
