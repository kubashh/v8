// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/cppgc/marker.h"

#include "include/cppgc/internal/process-heap.h"
#include "src/heap/cppgc/heap-object-header-inl.h"
#include "src/heap/cppgc/heap-page-inl.h"
#include "src/heap/cppgc/heap-visitor.h"
#include "src/heap/cppgc/heap.h"
#include "src/heap/cppgc/marking-visitor.h"
#include "src/heap/cppgc/stats-collector.h"

namespace cppgc {
namespace internal {

namespace {
template <typename Worklist, typename Callback>
bool DrainWorklistWithDeadline(v8::base::TimeTicks deadline, Worklist* worklist,
                               Callback callback, int task_id) {
  const size_t kDeadlineCheckInterval = 1250;

  size_t processed_callback_count = 0;
  typename Worklist::View view(worklist, task_id);
  typename Worklist::EntryType item;
  while (view.Pop(&item)) {
    callback(item);
    if (++processed_callback_count == kDeadlineCheckInterval) {
      if (deadline <= v8::base::TimeTicks::Now()) {
        return false;
      }
      processed_callback_count = 0;
    }
  }
  return true;
}
}  // namespace

constexpr int Marker::kMutatorThreadId;

Marker::Marker(HeapBase& heap)
    : heap_(heap), marking_visitor_(CreateMutatorThreadMarkingVisitor()) {}

Marker::~Marker() {
  // The fixed point iteration may have found not-fully-constructed objects.
  // Such objects should have already been found through the stack scan though
  // and should thus already be marked.
  if (!not_fully_constructed_worklist_.IsEmpty()) {
#if DEBUG
    DCHECK_NE(MarkingConfig::StackState::kNoHeapPointers, config_.stack_state);
    NotFullyConstructedItem item;
    NotFullyConstructedWorklist::View view(&not_fully_constructed_worklist_,
                                           kMutatorThreadId);
    while (view.Pop(&item)) {
      const HeapObjectHeader& header =
          BasePage::FromPayload(item)->ObjectHeaderFromInnerAddress(
              static_cast<ConstAddress>(item));
      DCHECK(header.IsMarked());
    }
#else
    not_fully_constructed_worklist_.Clear();
#endif
  }
}

void Marker::StartMarking(MarkingConfig config) {
  heap().stats_collector()->NotifyMarkingStarted();

  config_ = config;
  VisitRoots();
  EnterIncrementalMarkingIfNeeded(config);
}

void Marker::EnterAtomicPause(MarkingConfig config) {
  ExitIncrementalMarkingIfNeeded(config_);
  config_ = config;

  // VisitRoots also resets the LABs.
  VisitRoots();
  if (config_.stack_state == MarkingConfig::StackState::kNoHeapPointers) {
    FlushNotFullyConstructedObjects();
  } else {
    MarkNotFullyConstructedObjects();
  }
}

void Marker::FinishMarking() {
  heap().stats_collector()->NotifyMarkingCompleted(
      marking_visitor_->marked_bytes());
}

void Marker::FinishMarkingForTesting(MarkingConfig config) {
  EnterAtomicPause(config);
  AdvanceMarkingWithDeadline(v8::base::TimeDelta::Max());
  FinishMarking();
}

void Marker::ProcessWeakness() {
  heap().GetWeakPersistentRegion().Trace(marking_visitor_.get());

  // Call weak callbacks on objects that may now be pointing to dead objects.
  WeakCallbackItem item;
  LivenessBroker broker = LivenessBrokerFactory::Create();
  WeakCallbackWorklist::View view(&weak_callback_worklist_, kMutatorThreadId);
  while (view.Pop(&item)) {
    item.callback(broker, item.parameter);
  }
  // Weak callbacks should not add any new objects for marking.
  DCHECK(marking_worklist_.IsEmpty());
}

void Marker::VisitRoots() {
  // Reset LABs before scanning roots. LABs are cleared to allow
  // ObjectStartBitmap handling without considering LABs.
  heap().object_allocator().ResetLinearAllocationBuffers();

  heap().GetStrongPersistentRegion().Trace(marking_visitor_.get());
  if (config_.stack_state != MarkingConfig::StackState::kNoHeapPointers) {
    StackMarkingVisitor stack_marker(*marking_visitor_.get(),
                                     *heap().page_backend());
    heap().stack()->IteratePointers(&stack_marker);
  }
}

std::unique_ptr<MutatorThreadMarkingVisitor>
Marker::CreateMutatorThreadMarkingVisitor() {
  return std::make_unique<MutatorThreadMarkingVisitor>(this);
}

bool Marker::AdvanceMarkingWithDeadline(v8::base::TimeDelta duration) {
  MutatorThreadMarkingVisitor* visitor = marking_visitor_.get();
  v8::base::TimeTicks deadline = v8::base::TimeTicks::Now() + duration;

  do {
    // Convert |previously_not_fully_constructed_worklist_| to
    // |marking_worklist_|. This merely re-adds items with the proper
    // callbacks.
    if (!DrainWorklistWithDeadline(
            deadline, &previously_not_fully_constructed_worklist_,
            [visitor](NotFullyConstructedItem& item) {
              visitor->DynamicallyMarkAddress(
                  reinterpret_cast<ConstAddress>(item));
            },
            kMutatorThreadId))
      return false;

    if (!DrainWorklistWithDeadline(
            deadline, &marking_worklist_,
            [visitor](const MarkingItem& item) {
              const HeapObjectHeader& header =
                  HeapObjectHeader::FromPayload(item.base_object_payload);
              DCHECK(!MutatorThreadMarkingVisitor::IsInConstruction(header));
              item.callback(visitor, item.base_object_payload);
              visitor->AccountMarkedBytes(header);
            },
            kMutatorThreadId))
      return false;

    if (!DrainWorklistWithDeadline(
            deadline, &write_barrier_worklist_,
            [visitor](HeapObjectHeader* header) {
              DCHECK(header);
              DCHECK(!MutatorThreadMarkingVisitor::IsInConstruction(*header));
              const GCInfo& gcinfo =
                  GlobalGCInfoTable::GCInfoFromIndex(header->GetGCInfoIndex());
              gcinfo.trace(visitor, header->Payload());
              visitor->AccountMarkedBytes(*header);
            },
            kMutatorThreadId))
      return false;
  } while (!marking_worklist_.IsLocalViewEmpty(kMutatorThreadId));

  return true;
}

void Marker::FlushNotFullyConstructedObjects() {
  if (!not_fully_constructed_worklist_.IsLocalViewEmpty(kMutatorThreadId)) {
    not_fully_constructed_worklist_.FlushToGlobal(kMutatorThreadId);
    previously_not_fully_constructed_worklist_.MergeGlobalPool(
        &not_fully_constructed_worklist_);
  }
  DCHECK(not_fully_constructed_worklist_.IsLocalViewEmpty(kMutatorThreadId));
}

void Marker::MarkNotFullyConstructedObjects() {
  NotFullyConstructedItem item;
  NotFullyConstructedWorklist::View view(&not_fully_constructed_worklist_,
                                         kMutatorThreadId);
  while (view.Pop(&item)) {
    StackMarkingVisitor stack_marker(*marking_visitor_.get(),
                                     *heap().page_backend());
    stack_marker.ConservativelyMarkAddress(
        BasePage::FromPayload(item), reinterpret_cast<ConstAddress>(item));
  }
}

void Marker::ClearAllWorklistsForTesting() {
  marking_worklist_.Clear();
  not_fully_constructed_worklist_.Clear();
  previously_not_fully_constructed_worklist_.Clear();
  write_barrier_worklist_.Clear();
  weak_callback_worklist_.Clear();
}

void Marker::EnterIncrementalMarkingIfNeeded(Marker::MarkingConfig config) {
  if (config.marking_type == Marker::MarkingConfig::MarkingType::kIncremental ||
      config.marking_type ==
          Marker::MarkingConfig::MarkingType::kIncrementalAndConcurrent) {
    ProcessHeap::EnterIncrementalOrConcurrentMarking();
  }
}

void Marker::ExitIncrementalMarkingIfNeeded(Marker::MarkingConfig config) {
  if (config.marking_type == Marker::MarkingConfig::MarkingType::kIncremental ||
      config.marking_type ==
          Marker::MarkingConfig::MarkingType::kIncrementalAndConcurrent) {
    ProcessHeap::ExitIncrementalOrConcurrentMarking();
  }
}

}  // namespace internal
}  // namespace cppgc
