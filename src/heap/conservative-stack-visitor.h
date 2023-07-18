// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_CONSERVATIVE_STACK_VISITOR_H_
#define V8_HEAP_CONSERVATIVE_STACK_VISITOR_H_

#include <algorithm>
#include <limits>
#include <map>
#include <unordered_set>

#include "include/v8-internal.h"
#include "src/base/platform/mutex.h"
#include "src/common/globals.h"
#include "src/heap/base/stack.h"

namespace v8 {
namespace internal {

class MemoryAllocator;
class RootVisitor;

namespace measure_css {

class V8_EXPORT_PRIVATE ObjectStats {
 public:
  ObjectStats() { Clear(); }

  void AddObject(Address p);
  bool LookupObject(Address p) const;
  void Clear();
  bool IsClear() const;
  void PrintNVPOn(std::ostream& out) const;

  friend std::ostream& operator<<(std::ostream& out, const ObjectStats& s) {
    s.PrintNVPOn(out);
    return out;
  }

 private:
  mutable base::Mutex mutex_;
  std::unordered_set<Address> objects_;
};

class V8_EXPORT_PRIVATE Stats {
 public:
  enum CounterId {
    PRIMARY,
    SECONDARY,
    PAGE_NOT_FOUND,
    // What follows contains valid pointers inside pages.
    LARGE_PAGE,
    NORMAL_PAGE,
    FREE_SPACE,
    NOT_IN_YOUNG,
    YOUNG_FROM,
    ALREADY_MARKED,
    // What follows contains valid base pointers to objects.
    FULL_SHOULD_NOT_MARK,
    FULL_NOT_ALREADY_MARKED,
    FULL_ALREADY_MARKED,
    YOUNG_SHOULD_NOT_MARK,
    YOUNG_NOT_ALREADY_MARKED,
    YOUNG_ALREADY_MARKED,
    // What follows contains in addition object sizes.
    FALSE_POSITIVE,
    WOULD_BE_PINNED,
    BLACK_ALLOCATED,
    // This is for keeping track of how many counters we have.
    NUMBER_OF_COUNTERS
  };

  enum ValueId {
    ITER_FORWARD,
    ITER_BACKWARD_1,
    ITER_BACKWARD_2,
    // This is for keeping track of how many values we have.
    NUMBER_OF_VALUES
  };

  class V8_EXPORT_PRIVATE PointerStats {
   public:
    PointerStats() { Clear(); }

    bool AddSample(Address p, int size = 0, int multiplicity = 1) {
      count_ += multiplicity;
      if (auto it = histogram_.find(p); it == histogram_.end()) {
        histogram_[p] = multiplicity;
        ++unique_;
        size_ += size;
        return true;
      } else {
        it->second++;
        return false;
      }
    }

    void Clear() {
      count_ = unique_ = size_ = 0;
      histogram_.clear();
    }

    bool IsClear() const { return count_ == 0; }
    void PrintNVPOn(std::ostream& out) const;

    friend std::ostream& operator<<(std::ostream& out, const PointerStats& s) {
      s.PrintNVPOn(out);
      return out;
    }

    void SetId(CounterId id) { id_ = id; }

   private:
    CounterId id_;
    int64_t count_;
    int64_t unique_;
    int64_t size_;
    std::map<Address, int> histogram_;
  };

  template <typename T>
  class V8_EXPORT_PRIVATE ValueStats {
   public:
    ValueStats() { Clear(); }

    void AddSample(T value) {
      ++count_;
      sum_ += value;
      if (value > max_)
        max_ = value;
      else if (value < min_)
        min_ = value;
    }

    void Clear() {
      count_ = 0;
      sum_ = T(0);
      min_ = std::numeric_limits<T>::max();
      max_ = std::numeric_limits<T>::min();
    }
    bool IsClear() const { return count_ == 0; }
    void PrintNVPOn(std::ostream& out) const;

    friend std::ostream& operator<<(std::ostream& out, const ValueStats& s) {
      s.PrintNVPOn(out);
      return out;
    }

   private:
    int64_t count_ = 0;
    T sum_;
    T min_;
    T max_;
  };

  explicit Stats(Heap* heap) : heap_(heap) {
    for (int id = 0; id < NUMBER_OF_COUNTERS; ++id)
      pointers_[id].SetId(static_cast<CounterId>(id));
    Clear();
  }

  void AddPointer(Address p, CounterId id);
  void AddValue(Address p, ValueId id, int64_t value);

  void Clear() {
    std::for_each(pointers_.begin(), pointers_.end(),
                  [](PointerStats& s) { s.Clear(); });
    std::for_each(value_.begin(), value_.end(),
                  [](ValueStats<int64_t>& s) { s.Clear(); });
    size_false_positive_ = 0;
    size_would_be_pinned_ = 0;
    size_black_allocated_ = 0;
  }

  bool IsClear() const {
    return std::all_of(pointers_.begin(), pointers_.end(),
                       [](const PointerStats& s) { return s.IsClear(); }) &&
           std::all_of(
               value_.begin(), value_.end(),
               [](const ValueStats<int64_t>& s) { return s.IsClear(); });
  }

  void PrintNVPOn(std::ostream& out) const;

  friend std::ostream& operator<<(std::ostream& out, const Stats& s) {
    s.PrintNVPOn(out);
    return out;
  }

  ObjectStats* marked_objects() { return &marked_objects_; }

  std::pair<Address, int> FindObject(Address maybe_inner_ptr) const;

 private:
  using PointerInfo = std::pair<Address, std::pair<int, int>>;

  std::array<PointerStats, NUMBER_OF_COUNTERS> pointers_;
  std::array<ValueStats<int64_t>, NUMBER_OF_VALUES> value_;
  ObjectStats marked_objects_;
  Heap* const heap_;
  uint64_t size_false_positive_;
  uint64_t size_would_be_pinned_;
  uint64_t size_black_allocated_;
};

}  // namespace measure_css

class V8_EXPORT_PRIVATE ConservativeStackVisitor
    : public ::heap::base::StackVisitor {
 public:
  ConservativeStackVisitor(Isolate* isolate, RootVisitor* delegate);

  void VisitPointer(const void* pointer) final;

  // This method finds an object header based on a `maybe_inner_ptr`. It returns
  // `kNullAddress` if the parameter does not point to (the interior of) a valid
  // heap object, or if it points to (the interior of) some object that is
  // already marked as live (black or grey).
  // The GarbageCollector parameter is only used to determine which kind of
  // heap objects we are interested in. For MARK_COMPACTOR all heap objects are
  // considered, whereas for young generation collectors we only consider
  // objects in the young generation.
  static Address FindBasePtrForMarking(Address maybe_inner_ptr,
                                       MemoryAllocator* allocator,
                                       GarbageCollector collector,
                                       measure_css::Stats* stats);

 private:
  void VisitConservativelyIfPointer(Address address);
  measure_css::Stats* stats() const { return stats_; }

  const PtrComprCageBase cage_base_;
  RootVisitor* const delegate_;
  MemoryAllocator* const allocator_;
  const GarbageCollector collector_;
  measure_css::Stats* stats_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_CONSERVATIVE_STACK_VISITOR_H_
