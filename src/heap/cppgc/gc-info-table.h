// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_CPPGC_GC_INFO_TABLE_H_
#define V8_HEAP_CPPGC_GC_INFO_TABLE_H_

#include "include/cppgc/gc-info.h"
#include "include/cppgc/platform.h"
#include "src/base/logging.h"
#include "src/base/macros.h"
#include "src/base/platform/mutex.h"
#include "src/base/platform/platform.h"

namespace cppgc {
namespace internal {

class GCInfoTable final {
 public:
  // At maximum |kMaxIndex - 1| indices are supported.
  //
  // We assume that 14 bits is enough to represent all possible types.
  //
  // For Chromium during telemetry runs, we see about 1,000 different types;
  // looking at the output of the Oilpan GC Clang plugin, there appear to be at
  // most about 6,000 types. Thus 14 bits should be more than twice as many bits
  // as we will ever need.
  static constexpr GCInfoIndex kMaxIndex = 1 << 14;

  // Minimum index returned. Values smaller |kMinIndex| may be used as
  // sentinels.
  static constexpr GCInfoIndex kMinIndex = 1;

  // Refer through GlobalGCInfoTable for retrieving the global table outside
  // of testing code.
  explicit GCInfoTable(PageAllocator* page_allocator);

  GCInfoIndex EnsureGCInfoIndex(const GCInfo& info,
                                std::atomic<GCInfoIndex>* index_slot);

  const GCInfo& GCInfoFromIndex(GCInfoIndex index) const {
    DCHECK_GE(index, kMinIndex);
    DCHECK_LT(index, kMaxIndex);
    DCHECK(table_);
    const GCInfo* info = table_[index];
    DCHECK(info);
    return *info;
  }

  GCInfoIndex NumberOfGCInfosForTesting() const { return current_index_; }
  GCInfoIndex LimitForTesting() const { return limit_; }
  const GCInfo** TableSlotForTesting(GCInfoIndex index) {
    return &table_[index];
  }

 private:
  void Resize();

  GCInfoIndex InitialTableLimit() const;
  size_t MaxTableSize() const;

  PageAllocator* page_allocator_;
  // Holds the per-class GCInfo descriptors; each HeapObjectHeader keeps an
  // index into this table.
  const GCInfo** table_;
  // Current index used when requiring a new GCInfo object.
  GCInfoIndex current_index_ = kMinIndex;
  // The limit (exclusive) of the currently allocated table.
  GCInfoIndex limit_ = 0;

  v8::base::Mutex table_mutex_;

  DISALLOW_COPY_AND_ASSIGN(GCInfoTable);
};

class GlobalGCInfoTable final {
 public:
  // Sets up a singleton table that can be acquired using Get().
  static void Create(PageAllocator* page_allocator);

  // Accessors for the singleton table.
  static GCInfoTable* GetMutable() { return global_table_; }
  static const GCInfoTable& Get() { return *global_table_; }

  static const GCInfo& GCInfoFromIndex(GCInfoIndex index) {
    return Get().GCInfoFromIndex(index);
  }

 private:
  // Singleton for each process. Retrieved through Get().
  static GCInfoTable* global_table_;

  DISALLOW_NEW_AND_DELETE()
  DISALLOW_COPY_AND_ASSIGN(GlobalGCInfoTable);
};

}  // namespace internal
}  // namespace cppgc

#endif  // V8_HEAP_CPPGC_GC_INFO_TABLE_H_
