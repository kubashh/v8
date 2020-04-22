// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/read-only-heap.h"

#include <cstring>

#include "src/base/lazy-instance.h"
#include "src/base/lsan.h"
#include "src/base/platform/mutex.h"
#include "src/heap/heap-inl.h"
#include "src/heap/heap-write-barrier-inl.h"
#include "src/heap/spaces.h"
#include "src/objects/heap-object-inl.h"
#include "src/objects/objects-inl.h"
#include "src/objects/smi.h"
#include "src/snapshot/read-only-deserializer.h"

namespace v8 {
namespace internal {

#ifdef V8_SHARED_RO_HEAP
// Mutex used to ensure that ReadOnlyArtifacts creation is only done once.
base::LazyMutex read_only_heap_creation_mutex_ = LAZY_MUTEX_INITIALIZER;

// Weak pointer holding ReadOnlyArtifacts. ReadOnlyHeap::SetUp creates a
// std::shared_ptr from this when it attempts to reuse it. Since all Isolates
// hold a std::shared_ptr to this, the object is destroyed when no Isolates
// remain.
base::LazyInstance<std::weak_ptr<ReadOnlyArtifacts>>::type artifacts_ =
    LAZY_INSTANCE_INITIALIZER;
ReadOnlyHeap* ReadOnlyHeap::shared_ro_heap_ = nullptr;
#endif

// static
void ReadOnlyHeap::SetUp(Isolate* isolate, ReadOnlyDeserializer* des) {
  DCHECK_NOT_NULL(isolate);
#ifdef V8_SHARED_RO_HEAP

  bool read_only_heap_created = false;
  base::Optional<uint32_t> des_checksum;
#ifdef DEBUG
  if (des != nullptr) des_checksum = des->GetChecksum();
#endif  // DEBUG

  {
    base::MutexGuard guard(read_only_heap_creation_mutex_.Pointer());
    std::shared_ptr<ReadOnlyArtifacts> artifacts = artifacts_.Get().lock();
    if (!artifacts) {
      USE(des_checksum);
      shared_ro_heap_ = CreateAndAttachToIsolate(isolate);
      if (des != nullptr) {
#ifdef DEBUG
        shared_ro_heap_->read_only_blob_checksum_ = des_checksum;
#endif  // DEBUG
        shared_ro_heap_->DeseralizeIntoIsolate(isolate, des);
      }
      read_only_heap_created = true;
    }
  }

  USE(read_only_heap_created);
  USE(des_checksum);
#ifdef DEBUG
  const base::Optional<uint32_t> last_checksum =
      shared_ro_heap_->read_only_blob_checksum_;
  if (last_checksum) {
    // The read-only heap was set up from a snapshot. Make sure it's the always
    // the same snapshot.
    CHECK_WITH_MSG(des_checksum,
                   "Attempt to create the read-only heap after "
                   "already creating from a snapshot.");
    CHECK_EQ(last_checksum, des_checksum);
  } else {
    // The read-only heap objects were created. Make sure this happens only
    // once, during this call.
    CHECK(read_only_heap_created);
  }
#endif  // DEBUG

  isolate->SetUpFromReadOnlyHeap(shared_ro_heap_);
  if (des != nullptr) {
    void* const isolate_ro_roots = reinterpret_cast<void*>(
        isolate->roots_table().read_only_roots_begin().address());
    std::memcpy(isolate_ro_roots, shared_ro_heap_->read_only_roots_,
                kEntriesCount * sizeof(Address));
  }
#else
  auto* ro_heap = CreateAndAttachToIsolate(isolate);
  if (des != nullptr) ro_heap->DeseralizeIntoIsolate(isolate, des);
#endif  // V8_SHARED_RO_HEAP
}

void ReadOnlyHeap::DeseralizeIntoIsolate(Isolate* isolate,
                                         ReadOnlyDeserializer* des) {
  DCHECK_NOT_NULL(des);
  des->DeserializeInto(isolate);
  InitFromIsolate(isolate);
}

void ReadOnlyHeap::OnCreateHeapObjectsComplete(Isolate* isolate) {
  DCHECK_NOT_NULL(isolate);
  InitFromIsolate(isolate);
}

// static
ReadOnlyHeap* ReadOnlyHeap::CreateAndAttachToIsolate(Isolate* isolate) {
  auto* ro_heap = new ReadOnlyHeap(new ReadOnlySpace(isolate->heap()));
  isolate->SetUpFromReadOnlyHeap(ro_heap);
  return ro_heap;
}

void ReadOnlyHeap::InitFromIsolate(Isolate* isolate) {
  DCHECK(!init_complete_);
#ifdef V8_SHARED_RO_HEAP
  auto space_and_artifacts = read_only_space()->Detach();
  (*artifacts_.Pointer()) = space_and_artifacts.first;
  shared_ro_heap_->read_only_space_ = space_and_artifacts.second;

  isolate->SetReadOnlyArtifacts(space_and_artifacts.first);
  void* const isolate_ro_roots = reinterpret_cast<void*>(
      isolate->roots_table().read_only_roots_begin().address());
  std::memcpy(read_only_roots_, isolate_ro_roots,
              kEntriesCount * sizeof(Address));
  // N.B. Since pages are manually allocated with mmap, Lsan doesn't track their
  // pointers. Seal explicitly ignores the necessary objects.
  LSAN_IGNORE_OBJECT(this);
#else
  read_only_space_->Seal(ReadOnlySpace::SealMode::kDoNotDetachFromHeap);
#endif
  init_complete_ = true;
}

void ReadOnlyHeap::OnHeapTearDown() {
#ifndef V8_SHARED_RO_HEAP
  delete read_only_space_;
  delete this;
#endif
}

#ifdef V8_SHARED_RO_HEAP
// static
const ReadOnlyHeap* ReadOnlyHeap::Instance() { return shared_ro_heap_; }
#endif

// static
bool ReadOnlyHeap::Contains(Address address) {
  return MemoryChunk::FromAddress(address)->InReadOnlySpace();
}

// static
bool ReadOnlyHeap::Contains(HeapObject object) {
  if (V8_ENABLE_THIRD_PARTY_HEAP_BOOL) {
    return third_party_heap::Heap::InReadOnlySpace(object.address());
  } else {
    return MemoryChunk::FromHeapObject(object)->InReadOnlySpace();
  }
}

Object* ReadOnlyHeap::ExtendReadOnlyObjectCache() {
  read_only_object_cache_.push_back(Smi::zero());
  return &read_only_object_cache_.back();
}

Object ReadOnlyHeap::cached_read_only_object(size_t i) const {
  DCHECK_LE(i, read_only_object_cache_.size());
  return read_only_object_cache_[i];
}

bool ReadOnlyHeap::read_only_object_cache_is_initialized() const {
  return read_only_object_cache_.size() > 0;
}

ReadOnlyHeapObjectIterator::ReadOnlyHeapObjectIterator(ReadOnlyHeap* ro_heap)
    : ReadOnlyHeapObjectIterator(ro_heap->read_only_space()) {}

ReadOnlyHeapObjectIterator::ReadOnlyHeapObjectIterator(ReadOnlySpace* ro_space)
    : ro_space_(ro_space),
      current_page_(V8_ENABLE_THIRD_PARTY_HEAP_BOOL ? nullptr
                                                    : ro_space->first_page()),
      current_addr_(V8_ENABLE_THIRD_PARTY_HEAP_BOOL
                        ? Address()
                        : current_page_->area_start()) {}

HeapObject ReadOnlyHeapObjectIterator::Next() {
  if (V8_ENABLE_THIRD_PARTY_HEAP_BOOL) {
    return HeapObject();  // Unsupported
  }

  if (current_page_ == nullptr) {
    return HeapObject();
  }

  for (;;) {
    DCHECK_LE(current_addr_, current_page_->area_end());
    if (current_addr_ == current_page_->area_end()) {
      // Progress to the next page.
      current_page_ = current_page_->next_page();
      if (current_page_ == nullptr) {
        return HeapObject();
      }
      current_addr_ = current_page_->area_start();
    }

    if (current_addr_ == ro_space_->top() &&
        current_addr_ != ro_space_->limit()) {
      current_addr_ = ro_space_->limit();
      continue;
    }
    HeapObject object = HeapObject::FromAddress(current_addr_);
    const int object_size = object.Size();
    current_addr_ += object_size;

    if (object.IsFreeSpaceOrFiller()) {
      continue;
    }

    DCHECK_OBJECT_SIZE(object_size);
    return object;
  }
}

}  // namespace internal
}  // namespace v8
