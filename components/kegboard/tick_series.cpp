#include "tick_series.h"

namespace kbcore {

void TickSeries::reset(uint32_t resolution_ms) {
  this->count_ = 0;
  this->resolution_ms_ = resolution_ms;
  this->coarsened_ = false;
}

void TickSeries::add(uint32_t offset_ms, uint32_t ticks) {
  if (ticks == 0 || !this->enabled())
    return;

  // Snap to the start of the bucket this offset falls in, so repeated updates
  // within one interval accumulate into a single entry.
  uint32_t bucket = (offset_ms / this->resolution_ms_) * this->resolution_ms_;

  if (this->count_ > 0 && this->buckets_[this->count_ - 1].offset_ms == bucket) {
    this->buckets_[this->count_ - 1].ticks += ticks;
    return;
  }

  if (this->count_ == CAPACITY) {
    this->coarsen_();
    // Recompute against the new, wider resolution and retry the merge, since
    // this offset may now belong to the last bucket.
    bucket = (offset_ms / this->resolution_ms_) * this->resolution_ms_;
    if (this->count_ > 0 && this->buckets_[this->count_ - 1].offset_ms == bucket) {
      this->buckets_[this->count_ - 1].ticks += ticks;
      return;
    }
  }

  this->buckets_[this->count_].offset_ms = bucket;
  this->buckets_[this->count_].ticks = ticks;
  this->count_++;
}

void TickSeries::coarsen_() {
  // Merge buckets pairwise: [0,1] -> 0, [2,3] -> 1, ... Each merged bucket
  // keeps the earlier offset and the summed tick count, so no ticks are lost.
  size_t out = 0;
  for (size_t in = 0; in < this->count_; in += 2) {
    uint32_t ticks = this->buckets_[in].ticks;
    if (in + 1 < this->count_)
      ticks += this->buckets_[in + 1].ticks;
    this->buckets_[out].offset_ms = this->buckets_[in].offset_ms;
    this->buckets_[out].ticks = ticks;
    out++;
  }
  this->count_ = out;
  this->resolution_ms_ *= 2;
  this->coarsened_ = true;

  // Merging by position can leave two entries sharing a bucket once the
  // resolution widens; collapse any such neighbours so offsets stay unique
  // and strictly increasing.
  size_t write = 0;
  for (size_t read = 0; read < this->count_; read++) {
    uint32_t bucket = (this->buckets_[read].offset_ms / this->resolution_ms_) * this->resolution_ms_;
    if (write > 0 && this->buckets_[write - 1].offset_ms == bucket) {
      this->buckets_[write - 1].ticks += this->buckets_[read].ticks;
      continue;
    }
    this->buckets_[write].offset_ms = bucket;
    this->buckets_[write].ticks = this->buckets_[read].ticks;
    write++;
  }
  this->count_ = write;
}

std::string TickSeries::to_string() const {
  std::string out;
  if (this->count_ == 0)
    return out;

  // Rough reservation: offsets and counts are both short decimal strings.
  out.reserve(this->count_ * 12);
  for (size_t i = 0; i < this->count_; i++) {
    if (i != 0)
      out += ' ';
    out += std::to_string(this->buckets_[i].offset_ms);
    out += ':';
    out += std::to_string(this->buckets_[i].ticks);
  }
  return out;
}

}  // namespace kbcore
