// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef _INCLUDE_CACHE_MODEL_H_
#define _INCLUDE_CACHE_MODEL_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>
#include <ctime>

#include <unordered_map>
#include <atomic>
#include "memory-access-reasons.h"

#include <mutex>
#include "include/v8-platform.h"
#include "src/init/v8.h"

namespace v8 {
namespace internal {

struct CacheRefillEvent {
  std::time_t timestamp;
  heap_dump::MemoryAccessReason reason;
};

class V8_EXPORT_PRIVATE EightWaySetAssociativeCache {
  static constexpr uint8_t kInitialCountdown = 7;  // N - 1 for N-way cache.

  public:
    EightWaySetAssociativeCache(Heap* heap, size_t cache_line_size, size_t total_cache_size);
    bool Access(uint64_t address, bool should_refill, heap_dump::MemoryAccessReason reason);

    void DumpRefillEvents(std::ostream&) const;
    void InvalidateCache();
    void DumpCacheHitRate() const;

  private:
    struct CacheLine {
      size_t tag = 0;
      bool valid = false;
      uint8_t countdown = 0;

      bool operator<(const CacheLine& other) const {
        return countdown < other.countdown;
      }
    };

    Heap* const heap_;
    /**
     * returns true if it's a hit.
     *
     * If it is a hit, the LRU records are always updated.
     * If it is a miss AND should_refill is true, then one of the LRU entry will be removed, and
     * replaced with address.
     */
    bool _Access(uint64_t address, bool should_refill, heap_dump::MemoryAccessReason reason);

    [[maybe_unused]]const size_t cache_line_size_;
    const size_t cache_line_size_log_2_;
    const size_t num_sets_;
    const size_t num_sets_log_2_;

    std::mutex m;
    std::vector<uint64_t> hit_count;
    std::vector<uint64_t> access_count;

    std::vector<std::array<CacheLine, 8>> cache_sets_;

    std::vector<CacheRefillEvent> refill_events_;
};

}  // namespace internal
}  // namespace v8

#endif /* #ifndef _INCLUDE_CACHE_MODEL_H_ */
