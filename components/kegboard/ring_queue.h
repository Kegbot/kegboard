#pragma once

// Fixed-capacity FIFO.
//
// Part of the framework-agnostic kegboard core: no ESPHome, Arduino, or
// ESP-IDF headers. See CORE.md.

#include <cstddef>
#include <cstdint>

namespace kbcore {

/// A bounded FIFO backed by a fixed array, for buffering reports that could
/// not be delivered yet.
///
/// Capacity is fixed at compile time so the memory cost of an outage is known
/// up front and a long one cannot exhaust the heap. When the queue is full,
/// push() evicts the oldest entry and increments dropped(); callers are
/// expected to surface that counter, because it means data loss.
///
/// Evicting the oldest rather than rejecting the newest is a deliberate
/// choice: during a prolonged outage the newest reports are the ones a user is
/// most likely to be watching for, and an unbounded backlog of stale pours is
/// not worth the pours being poured right now.
template<typename T, size_t N> class RingQueue {
 public:
  static constexpr size_t capacity() { return N; }

  size_t size() const { return this->size_; }
  bool empty() const { return this->size_ == 0; }
  bool full() const { return this->size_ == N; }

  /// Number of entries discarded because the queue was full.
  uint32_t dropped() const { return this->dropped_; }

  /// Append an item. Returns false if an older item had to be evicted.
  bool push(const T &item) {
    bool evicted = false;
    if (this->size_ == N) {
      this->head_ = (this->head_ + 1) % N;
      this->size_--;
      this->dropped_++;
      evicted = true;
    }
    size_t tail = (this->head_ + this->size_) % N;
    this->items_[tail] = item;
    this->size_++;
    return !evicted;
  }

  /// Oldest item, or nullptr when empty. Valid until the next mutation.
  const T *peek() const { return this->size_ == 0 ? nullptr : &this->items_[this->head_]; }

  /// Item `i` positions from the oldest, or nullptr past the end. Lets a
  /// sender assemble a batch without popping anything until it is accepted.
  const T *at(size_t i) const { return i >= this->size_ ? nullptr : &this->items_[(this->head_ + i) % N]; }

  /// Remove the oldest item, optionally copying it to `out`.
  bool pop(T *out = nullptr) {
    if (this->size_ == 0)
      return false;
    if (out != nullptr)
      *out = this->items_[this->head_];
    this->items_[this->head_] = T{};
    this->head_ = (this->head_ + 1) % N;
    this->size_--;
    return true;
  }

  void clear() {
    while (this->pop()) {
    }
  }

 private:
  T items_[N]{};
  size_t head_{0};
  size_t size_{0};
  uint32_t dropped_{0};
};

}  // namespace kbcore
