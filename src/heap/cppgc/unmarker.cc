// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/cppgc/unmarker.h"

#include <algorithm>
#include <atomic>
#include <vector>

#include "src/heap/cppgc/heap-base.h"
#include "src/heap/cppgc/heap-object-header-inl.h"
#include "src/heap/cppgc/heap-page.h"
#include "src/heap/cppgc/heap-visitor.h"
#include "src/heap/cppgc/object-start-bitmap-inl.h"
#include "src/heap/cppgc/raw-heap.h"

namespace cppgc {
namespace internal {

namespace {

class AtomicUnmarkVisitor final : public HeapVisitor<AtomicUnmarkVisitor> {
 public:
  bool VisitHeapObjectHeader(HeapObjectHeader* header) {
    if (header->IsMarked()) header->Unmark();
    return true;
  }
};

using UnmarkedPages = std::vector<BasePage*>;

// This visitor:
// - clears free lists for all spaces;
// - moves all Heap pages.
class PrepareForConcurrentUnmarkVisitor final
    : public HeapVisitor<PrepareForConcurrentUnmarkVisitor> {
 public:
  explicit PrepareForConcurrentUnmarkVisitor(RawHeap* heap) { Traverse(heap); }

  UnmarkedPages GetPages() { return std::move(unmarked_pages_); }

  bool VisitNormalPageSpace(NormalPageSpace* space) {
    AddPages(space);
    return true;
  }

  bool VisitLargePageSpace(LargePageSpace* space) {
    AddPages(space);
    return true;
  }

 private:
  void AddPages(BaseSpace* space) {
    unmarked_pages_.insert(unmarked_pages_.end(), space->begin(), space->end());
  }

  UnmarkedPages unmarked_pages_;
};

// Unmarks pages concurrently.
class ConcurrentUnmarkTask final : public JobTask,
                                   private HeapVisitor<ConcurrentUnmarkTask> {
  friend class HeapVisitor<ConcurrentUnmarkTask>;

 public:
  explicit ConcurrentUnmarkTask(UnmarkedPages&& pages)
      : pages_(std::move(pages)) {}

  void Run(v8::JobDelegate* delegate) final {
    for (auto* page : pages_) {
      Traverse(page);
      if (delegate->ShouldYield()) return;
    }
    is_completed_.store(true, std::memory_order_relaxed);
  }

  size_t GetMaxConcurrency() const final {
    return is_completed_.load(std::memory_order_relaxed) ? 0 : 1;
  }

 private:
  bool VisitNormalPage(NormalPage* page) {
    static constexpr auto kAtomic = HeapObjectHeader::AccessMode::kAtomic;
    page->object_start_bitmap().Iterate([](Address addr) {
      auto* header = reinterpret_cast<HeapObjectHeader*>(addr);
      auto header_copy =
          reinterpret_cast<std::atomic<HeapObjectHeader>&>(*header).load(
              std::memory_order_acquire);
      if (header_copy.IsMarked()) header->Unmark<kAtomic>();
    });
    return true;
  }

  bool VisitLargePage(LargePage* page) {
    static constexpr auto kAtomic = HeapObjectHeader::AccessMode::kAtomic;
    HeapObjectHeader* header = page->ObjectHeader();
    if (header->IsMarked<kAtomic>()) header->Unmark<kAtomic>();
    return true;
  }

  UnmarkedPages pages_;
  std::atomic_bool is_completed_{false};
};

}  // namespace

class Unmarker::Impl final {
 public:
  Impl(HeapBase* heap, Platform* platform) : heap_(heap), platform_(platform) {}

  void Start(Config config) {
    if (config == Config::kAtomic) {
      AtomicUnmarkVisitor visitor;
      visitor.Traverse(&heap_->raw_heap());
    } else {
      DCHECK_EQ(Config::kConcurrent, config);
      PrepareForConcurrentUnmarkVisitor prepare_visitor(&heap_->raw_heap());
      auto pages = prepare_visitor.GetPages();
      job_handle_ = platform_->PostJob(
          TaskPriority::kUserVisible,
          std::make_unique<ConcurrentUnmarkTask>(std::move(pages)));
    }
  }

  void Finish() { job_handle_->Join(); }

 private:
  HeapBase* heap_;
  Platform* platform_;
  std::unique_ptr<JobHandle> job_handle_;
};

Unmarker::Unmarker(HeapBase* heap, Platform* platform)
    : impl_(std::make_unique<Impl>(heap, platform)) {}

Unmarker::~Unmarker() = default;

void Unmarker::Start(Config config) { impl_->Start(std::move(config)); }
void Unmarker::Finish() { impl_->Finish(); }

}  // namespace internal
}  // namespace cppgc
