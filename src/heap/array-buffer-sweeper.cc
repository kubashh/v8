// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/array-buffer-sweeper.h"
#include "src/heap/gc-tracer.h"
#include "src/heap/heap-inl.h"
#include "src/objects/js-array-buffer.h"
#include "src/tasks/cancelable-task.h"
#include "src/tasks/task-utils.h"

namespace v8 {
namespace internal {

void ArrayBufferList::Append(ArrayBufferExtension* extension) {
  if (head_ == nullptr) {
    DCHECK_NULL(tail_);
    head_ = tail_ = extension;
  } else {
    tail_->set_next(extension);
    tail_ = extension;
  }

  extension->set_next(nullptr);
}

void ArrayBufferList::Append(ArrayBufferList* list) {
  if (head_ == nullptr) {
    DCHECK_NULL(tail_);
    head_ = list->head_;
    tail_ = list->tail_;
  } else if (list->head_) {
    DCHECK_NOT_NULL(list->tail_);
    tail_->set_next(list->head_);
    tail_ = list->tail_;
  } else {
    DCHECK_NULL(list->tail_);
  }

  list->Reset();
}

bool ArrayBufferList::Find(ArrayBufferExtension* extension) {
  ArrayBufferExtension* current = head_;

  while (current) {
    if (current == extension) return true;
    current = current->next();
  }

  return false;
}

void ArrayBufferSweeper::EnsureFinished() {
  if (sweeping_task_ == SweepingTask::None) return;

  if (heap_->isolate()->cancelable_task_manager()->TryAbort(
          sweeping_task_id_) != TryAbortResult::kTaskAborted) {
    base::MutexGuard guard(&sweeping_mutex_);
    if (!sweeping_finished_) PerformSweep();
    MergeSweepingLists();
  } else {
    PerformSweep();
    MergeSweepingLists();
  }

  sweeping_task_id_ = 0;
  sweeping_task_ = SweepingTask::None;
}

void ArrayBufferSweeper::RequestSweepYoung() {
  RequestSweep(SweepingTask::Young);
}

void ArrayBufferSweeper::RequestSweepFull() {
  RequestSweep(SweepingTask::Full);
}

void ArrayBufferSweeper::RequestSweep(SweepingTask sweeping_task) {
  DCHECK_EQ(sweeping_task_, SweepingTask::None);

  if (!heap_->IsTearingDown() && !heap_->ShouldReduceMemory() &&
      FLAG_concurrent_array_buffer_sweeping) {
    {
      base::MutexGuard guard(&sweeping_mutex_);
      Prepare(sweeping_task);
    }

    auto task = MakeCancelableTask(heap_->isolate(), [this] {
      TRACE_BACKGROUND_GC(
          heap_->tracer(),
          GCTracer::BackgroundScope::BACKGROUND_ARRAY_BUFFER_SWEEP);
      base::MutexGuard guard(&sweeping_mutex_);
      PerformSweep();
    });
    sweeping_task_id_ = task->id();
    V8::GetCurrentPlatform()->CallOnWorkerThread(std::move(task));
    sweeping_task_ = sweeping_task;
  } else {
    Prepare(sweeping_task);
    PerformSweep();
    MergeSweepingLists();
    sweeping_task_ = SweepingTask::None;
  }
}

void ArrayBufferSweeper::Prepare(SweepingTask task) {
  DCHECK(sweeping_young_.IsEmpty());
  DCHECK(sweeping_old_.IsEmpty());

  sweeping_finished_ = false;

  if (task == SweepingTask::Young) {
    sweeping_young_ = young_;
    young_.Reset();

    sweeping_task_ = SweepingTask::Young;
  } else {
    CHECK_EQ(task, SweepingTask::Full);
    sweeping_young_ = young_;
    young_.Reset();

    sweeping_old_ = old_;
    old_.Reset();

    sweeping_task_ = SweepingTask::Full;
  }
}

void ArrayBufferSweeper::PerformSweep() {
  if (sweeping_task_ == SweepingTask::Young) {
    PerformSweepYoung();
  } else {
    PerformSweepFull();
  }
  sweeping_finished_ = true;
}

void ArrayBufferSweeper::PerformSweepFull() {
  CHECK_EQ(sweeping_task_, SweepingTask::Full);
  ArrayBufferList promoted = SweepListFull(&sweeping_young_);
  ArrayBufferList survived = SweepListFull(&sweeping_old_);

  sweeping_old_ = promoted;
  sweeping_old_.Append(&survived);
}

ArrayBufferList ArrayBufferSweeper::SweepListFull(ArrayBufferList* list) {
  ArrayBufferExtension* current = list->head_;
  ArrayBufferList survived;

  while (current) {
    ArrayBufferExtension* next = current->next();

    if (!current->IsMarked()) {
      delete current;
    } else {
      current->Unmark();
      survived.Append(current);
    }

    current = next;
  }

  list->Reset();
  return survived;
}

void ArrayBufferSweeper::PerformSweepYoung() {
  CHECK_EQ(sweeping_task_, SweepingTask::Young);
  ArrayBufferExtension* current = sweeping_young_.head_;
  DCHECK(sweeping_old_.IsEmpty());

  ArrayBufferList young;
  ArrayBufferList old;

  while (current) {
    ArrayBufferExtension* next = current->next();

    if (!current->IsYoungMarked()) {
      delete current;
    } else if (current->IsYoungPromoted()) {
      current->YoungUnmark();
      old.Append(current);
    } else {
      current->YoungUnmark();
      young.Append(current);
    }

    current = next;
  }

  sweeping_old_ = old;
  sweeping_young_ = young;
}

void ArrayBufferSweeper::MergeSweepingLists() {
  young_.Append(&sweeping_young_);
  old_.Append(&sweeping_old_);
}

void ArrayBufferSweeper::ReleaseAll() {
  EnsureFinished();
  ReleaseAll(&old_);
  ReleaseAll(&young_);
}

void ArrayBufferSweeper::ReleaseAll(ArrayBufferList* list) {
  ArrayBufferExtension* current = list->head_;

  while (current) {
    ArrayBufferExtension* next = current->next();
    delete current;
    current = next;
  }

  list->Reset();
}

void ArrayBufferSweeper::Append(JSArrayBuffer object,
                                ArrayBufferExtension* extension) {
  if (Heap::InYoungGeneration(object)) {
    young_.Append(extension);
  } else {
    old_.Append(extension);
  }
}

}  // namespace internal
}  // namespace v8
