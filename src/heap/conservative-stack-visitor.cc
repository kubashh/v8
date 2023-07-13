// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/conservative-stack-visitor.h"

#include "src/execution/isolate-inl.h"
#include "src/heap/basic-memory-chunk.h"
#include "src/heap/marking-inl.h"
#include "src/objects/visitors.h"

#ifdef V8_COMPRESS_POINTERS
#include "src/common/ptr-compr-inl.h"
#endif  // V8_COMPRESS_POINTERS

namespace v8 {
namespace internal {

ConservativeStackVisitor::ConservativeStackVisitor(Isolate* isolate,
                                                   RootVisitor* delegate)
    : cage_base_(isolate),
      delegate_(delegate),
      allocator_(isolate->heap()->memory_allocator()),
      collector_(delegate->collector()),
      stats_(isolate->heap()->css_stats()) {}

// static
Address ConservativeStackVisitor::FindBasePtrForMarking(
    Address maybe_inner_ptr, MemoryAllocator* allocator,
    GarbageCollector collector, measure_css::Stats* stats) {
  // Check if the pointer is contained by a normal or large page owned by this
  // heap. Bail out if it is not.
  const BasicMemoryChunk* chunk =
      allocator->LookupChunkContainingAddress(maybe_inner_ptr);
  if (chunk == nullptr) {
    stats->AddPointer(maybe_inner_ptr, measure_css::Stats::PAGE_NOT_FOUND);
    return kNullAddress;
  }
  DCHECK(chunk->Contains(maybe_inner_ptr));
  // If it is contained in a large page, we want to mark the only object on it.
  if (chunk->IsLargePage()) {
    stats->AddPointer(maybe_inner_ptr, measure_css::Stats::LARGE_PAGE);
    // This could be simplified if we could guarantee that there are no free
    // space or filler objects in large pages. A few cctests violate this now.
    HeapObject obj(static_cast<const LargePage*>(chunk)->GetObject());
    PtrComprCageBase cage_base{chunk->heap()->isolate()};
    if (obj.IsFreeSpaceOrFiller(cage_base)) {
      stats->AddPointer(obj.address(), measure_css::Stats::FREE_SPACE);
      return kNullAddress;
    } else {
      return obj.address();
    }
  }
  // Otherwise, we have a pointer inside a normal page.
  stats->AddPointer(maybe_inner_ptr, measure_css::Stats::NORMAL_PAGE);
  const Page* page = static_cast<const Page*>(chunk);
  // If it is not in the young generation and we're only interested in young
  // generation pointers, we must ignore it.
  if (Heap::IsYoungGenerationCollector(collector) &&
      !page->InYoungGeneration()) {
    stats->AddPointer(maybe_inner_ptr, measure_css::Stats::NOT_IN_YOUNG);
    return kNullAddress;
  }
  // If it is in the young generation "from" semispace, it is not used and we
  // must ignore it, as its markbits may not be clean.
  if (page->IsFromPage()) {
    stats->AddPointer(maybe_inner_ptr, measure_css::Stats::YOUNG_FROM);
    return kNullAddress;
  }
  // Try to find the address of a previous valid object on this page.
  Address base_ptr = MarkingBitmap::FindPreviousObjectForConservativeMarking(
      page, maybe_inner_ptr, stats);
  // If the markbit is set, then we have an object that does not need to be
  // marked.
  if (base_ptr == kNullAddress) {
    stats->AddPointer(maybe_inner_ptr, measure_css::Stats::ALREADY_MARKED);
    return kNullAddress;
  }
  // Iterate through the objects in the page forwards, until we find the object
  // containing maybe_inner_ptr.
  DCHECK_LE(base_ptr, maybe_inner_ptr);
  PtrComprCageBase cage_base{page->heap()->isolate()};
  for (int iterations = 0; true; ++iterations) {
    HeapObject obj(HeapObject::FromAddress(base_ptr));
    const int size = obj.Size(cage_base);
    DCHECK_LT(0, size);
    if (maybe_inner_ptr < base_ptr + size) {
      stats->AddValue(maybe_inner_ptr, measure_css::Stats::ITER_FORWARD,
                      iterations);
      if (obj.IsFreeSpaceOrFiller(cage_base)) {
        stats->AddPointer(obj.address(), measure_css::Stats::FREE_SPACE);
        return kNullAddress;
      } else {
        return base_ptr;
      }
    }
    base_ptr += size;
    DCHECK_LT(base_ptr, page->area_end());
  }
}

void ConservativeStackVisitor::VisitPointer(const void* pointer) {
  auto address = reinterpret_cast<Address>(const_cast<void*>(pointer));
  stats()->AddPointer(address, measure_css::Stats::PRIMARY);
  VisitConservativelyIfPointer(address);
#ifdef V8_COMPRESS_POINTERS
  V8HeapCompressionScheme::ProcessIntermediatePointers(
      cage_base_, address,
      [this](Address ptr) { VisitConservativelyIfPointer(ptr); });
#endif  // V8_COMPRESS_POINTERS
}

void ConservativeStackVisitor::VisitConservativelyIfPointer(Address address) {
  stats()->AddPointer(address, measure_css::Stats::SECONDARY);
  Address base_ptr =
      FindBasePtrForMarking(address, allocator_, collector_, stats());
  if (base_ptr == kNullAddress) return;
  HeapObject obj = HeapObject::FromAddress(base_ptr);
  Object root = obj;
  delegate_->VisitRootPointer(Root::kConservativeStackRoots, nullptr,
                              FullObjectSlot(&root));
  // Check that the delegate visitor did not modify the root slot.
  DCHECK_EQ(root, obj);
}

void measure_css::ObjectStats::Clear() {
  base::MutexGuard lock(&mutex_);
  objects_.clear();
}

bool measure_css::ObjectStats::IsClear() const {
  base::MutexGuard lock(&mutex_);
  return objects_.empty();
}

void measure_css::ObjectStats::AddObject(Address p) {
  base::MutexGuard lock(&mutex_);
  auto it = objects_.find(p);
  CHECK_EQ(objects_.end(), it);
  objects_.insert(p);
}

bool measure_css::ObjectStats::LookupObject(Address p) const {
  base::MutexGuard lock(&mutex_);
  auto it = objects_.find(p);
  return it != objects_.end();
}

void measure_css::PointerStats::PrintNVPOn(std::ostream& out) const {
  out << "{\"count\": " << count_ << ",\"unique\": " << unique_
      << ",\"histogram\": [";
  constexpr int number_of_buckets = 10;
  std::vector<int> h(number_of_buckets);
  for (auto [pointer, multiplicity] : histogram_) {
    // Assume pointer compression; this works regardless but is probably bogus.
    int bucket = static_cast<int64_t>(static_cast<uint32_t>(pointer)) *
                 number_of_buckets / 0x100000000;
    h[bucket] += multiplicity;
  }
  bool first = true;
  for (int c : h) {
    out << (first ? "" : ",") << c;
    first = false;
  }
  out << "]}";
}

template <typename T>
void measure_css::ValueStats<T>::PrintNVPOn(std::ostream& out) const {
  out << "{\"count\": " << count_;
  if (count_ > 0)
    out << ",\"sum\": " << sum_ << ",\"min\": " << min_ << ",\"max\": " << max_
        << ",\"avg\": " << double(sum_) / count_;
  out << "}";
}

void measure_css::ObjectStats::PrintNVPOn(std::ostream& out) const {
  out << "{\"count\": " << objects_.size() << "}";
}

namespace {
bool IsBlackAllocated(Address ptr, int size) {
  Page* p = Page::FromAddress(ptr);
  MarkingBitmap::MarkBitIndex start = MarkingBitmap::AddressToIndex(ptr);
  MarkingBitmap::MarkBitIndex end =
      MarkingBitmap::LimitAddressToIndex(ptr + size);
  return p->marking_bitmap()->AllBitsSetInRange(start, end);
}
}  // namespace

void measure_css::Stats::AddPointer(Address p, CounterId id) {
  pointers_[id].AddSample(p);
  if (id != ALREADY_MARKED && id != FULL_ALREADY_MARKED &&
      id != FULL_NOT_ALREADY_MARKED && id != YOUNG_ALREADY_MARKED &&
      id != YOUNG_NOT_ALREADY_MARKED)
    return;
  auto [base_ptr, size] = FindObject(p);
  if (base_ptr == kNullAddress) return;  // Free space or filler.
  bool in_marked = marked_objects_.LookupObject(base_ptr);
  if (id == FULL_NOT_ALREADY_MARKED || id == YOUNG_NOT_ALREADY_MARKED) {
    // The object should definitely be marked now.
    CHECK(in_marked);
    if (pointers_[FALSE_POSITIVE].AddSample(base_ptr))
      size_false_positive_ += size;
  } else if (id == ALREADY_MARKED && !in_marked) {
    CHECK(IsBlackAllocated(base_ptr, size));
    if (pointers_[BLACK_ALLOCATED].AddSample(base_ptr))
      size_black_allocated_ += size;
    if (pointers_[WOULD_BE_PINNED].AddSample(base_ptr))
      size_would_be_pinned_ += size;
  } else {
    CHECK(in_marked);
    if (pointers_[WOULD_BE_PINNED].AddSample(base_ptr))
      size_would_be_pinned_ += size;
  }
}

void measure_css::Stats::AddValue(Address p, ValueId id, int64_t value) {
  value_[id].AddSample(value);
}

std::pair<Address, int> measure_css::Stats::FindObject(
    Address maybe_inner_ptr) const {
  const BasicMemoryChunk* chunk =
      heap_->memory_allocator()->LookupChunkContainingAddress(maybe_inner_ptr);
  CHECK_NOT_NULL(chunk);
  CHECK(chunk->Contains(maybe_inner_ptr));
  if (chunk->IsLargePage()) {
    HeapObject obj(static_cast<const LargePage*>(chunk)->GetObject());
    PtrComprCageBase cage_base{chunk->heap()->isolate()};
    int size = obj.Size(cage_base);
    if (obj.IsFreeSpaceOrFiller(cage_base)) {
      return std::make_pair(kNullAddress, size);
    } else {
      return std::make_pair(obj.address(), size);
    }
  }
  const Page* page = static_cast<const Page*>(chunk);
  Address base_ptr = page->area_start();
  DCHECK_LE(base_ptr, maybe_inner_ptr);
  PtrComprCageBase cage_base{page->heap()->isolate()};
  while (true) {
    HeapObject obj(HeapObject::FromAddress(base_ptr));
    const int size = obj.Size(cage_base);
    DCHECK_LT(0, size);
    if (maybe_inner_ptr < base_ptr + size) {
      if (obj.IsFreeSpaceOrFiller(cage_base)) {
        return std::make_pair(kNullAddress, size);
      } else {
        return std::make_pair(base_ptr, size);
      }
    }
    base_ptr += size;
    DCHECK_LT(base_ptr, page->area_end());
  }
}

void measure_css::Stats::PrintNVPOn(std::ostream& out) const {
  out << "{\"primary\": " << pointers_[PRIMARY]
      << ",\"secondary\": " << pointers_[SECONDARY]
      << ",\"page not found\": " << pointers_[PAGE_NOT_FOUND]
      << ",\"large page\": " << pointers_[LARGE_PAGE]
      << ",\"normal page\": " << pointers_[NORMAL_PAGE]
      << ",\"free space\": " << pointers_[FREE_SPACE]
      << ",\"not in young\": " << pointers_[NOT_IN_YOUNG]
      << ",\"young from\": " << pointers_[YOUNG_FROM]
      << ",\"already marked\": " << pointers_[ALREADY_MARKED]
      << ",\"full, should not mark\": " << pointers_[FULL_SHOULD_NOT_MARK]
      << ",\"full, not already marked\": " << pointers_[FULL_NOT_ALREADY_MARKED]
      << ",\"full, already marked\": " << pointers_[FULL_ALREADY_MARKED]
      << ",\"young, should not mark\": " << pointers_[YOUNG_SHOULD_NOT_MARK]
      << ",\"young, not already marked\": "
      << pointers_[YOUNG_NOT_ALREADY_MARKED]
      << ",\"young, already marked\": " << pointers_[YOUNG_ALREADY_MARKED]
      << ",\"iter backward 1\": " << value_[ITER_BACKWARD_1]
      << ",\"iter backward 2\": " << value_[ITER_BACKWARD_2]
      << ",\"iter forward\": " << value_[ITER_FORWARD]
      << ",\"marked_objects\": " << marked_objects_
      << ",\"false positive\": " << pointers_[FALSE_POSITIVE]
      << ",\"would be pinned\": " << pointers_[WOULD_BE_PINNED]
      << ",\"black allocated\": " << pointers_[BLACK_ALLOCATED]
      << ",\"size of false positive\": " << size_false_positive_
      << ",\"size of would be pinned\": " << size_would_be_pinned_
      << ",\"size of black allocated\": " << size_black_allocated_ << "}";
}

}  // namespace internal
}  // namespace v8
