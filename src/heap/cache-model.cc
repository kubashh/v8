// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <cassert>
#include <iostream>

#include "src/execution/isolate.h"
#include "src/heap/cache-model.h"
#include "src/heap/heap-inl.h"
#include "src/heap/heap.h"
#include "src/logging/log.h"

namespace v8 {
namespace internal {

size_t Log2(size_t n) {
  assert( n > 0 && (n & (n - 1)) == 0 );
  return __builtin_ctzll(n);
}

void EightWaySetAssociativeCache::InvalidateCache() {

  for (auto& cache_set : cache_sets_) {
    for (auto& cache_line : cache_set) {
      cache_line.valid = false;
      cache_line.tag = 0;
      cache_line.countdown = 0;
    }
  }
  for (size_t i = 0; i < heap_dump::kNumMemoryAccessReasons; i++) {
    hit_count[i] = access_count[i] = 0;
  }
}

EightWaySetAssociativeCache::EightWaySetAssociativeCache(
    Heap* heap, size_t cache_line_size, size_t total_cache_size) :
  heap_(heap),
  cache_line_size_(cache_line_size),
  cache_line_size_log_2_(Log2(cache_line_size)),
  num_sets_(total_cache_size / cache_line_size / 8),
  num_sets_log_2_(Log2(total_cache_size / cache_line_size / 8)),
  hit_count(heap_dump::kNumMemoryAccessReasons),
  access_count(heap_dump::kNumMemoryAccessReasons)
{
  cache_sets_.resize(num_sets_);

  InvalidateCache();

}

bool EightWaySetAssociativeCache::_Access(uint64_t address, bool should_refill,
                                         heap_dump::MemoryAccessReason reason) {
  size_t cache_line_index = address >> cache_line_size_log_2_;
  size_t set_index = cache_line_index & (num_sets_ - 1);
  size_t tag = cache_line_index >> num_sets_log_2_;
  auto& cache_set = cache_sets_[set_index];

  auto update_countdown = [&](CacheLine* chosen) {
    assert(chosen != nullptr);
    for (auto& cache_line : cache_set) {
      if (&cache_line == chosen) {
        continue;
      }
      if (*chosen < cache_line) {
        assert(cache_line.countdown > 0);
        cache_line.countdown--;
      }
    }
    chosen->valid = true;
    chosen->tag = tag;
    chosen->countdown = kInitialCountdown;
  };

  for (auto& cache_line : cache_set) {
    if (cache_line.valid && cache_line.tag == tag) {
      update_countdown(&cache_line);

      return true;
    }
  }

  if (should_refill) {
    // Records all cache refill events

    auto const now = std::chrono::system_clock::now();
    std::time_t newt = std::chrono::system_clock::to_time_t(now);
    refill_events_.push_back(
      CacheRefillEvent{
        .timestamp = newt,
        .reason = reason,
      }
    );

    CacheLine* chosen = &(*std::min_element(cache_set.begin(), cache_set.end()));
    update_countdown(chosen);
  }
  return false;
}

bool EightWaySetAssociativeCache::Access(uint64_t address, bool should_refill,
                                         heap_dump::MemoryAccessReason reason) {

  // Find the index corresponding to the 'reason'
  size_t index = 0;
  while (reason != (heap_dump::MemoryAccessReason)(1 << index)) {
    index++;
  }

  //FIXME: Prevent race condition in a brute force way, fix this if we encounter performance issue
  m.lock();
  bool is_hit = _Access(address, should_refill, reason);
  if (is_hit) {
      hit_count[index]++;
  }
  access_count[index]++;
  m.unlock();

  return is_hit;
}

void EightWaySetAssociativeCache::DumpCacheHitRate() const {
  size_t hit_sum = 0;
  size_t total_sum = 0;

  for (size_t i = 0; i < heap_dump::kNumMemoryAccessReasons; i++) {
    auto ratio = (access_count[i] == 0)? 0 : 1.0 * hit_count[i] / access_count[i];

    std::string s = std::format("{} Cache hit ratio: {:.2f} Total Cache access: {}",
        heap_dump::MemoryAccessReasonNames[i], ratio, access_count[i]);
    LOG(heap_->isolate(), CacheInfo(s.c_str()));

    hit_sum += hit_count[i];
    total_sum += access_count[i];
  }

  auto ratio = (total_sum == 0)? 0 : 1.0 * hit_sum / total_sum;

  std::string s = std::format("Overall Cache hit ratio: {:.2f} Total Cache access: {}",
          ratio, total_sum);
  LOG(heap_->isolate(), CacheInfo(s.c_str()));
}

}  // namespace internal
}  // namespace v8
