// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if defined(CPPGC_YOUNG_GENERATION)

#include "src/heap/cppgc/unmarker.h"

#include "src/heap/cppgc/heap-object-header-inl.h"
#include "src/heap/cppgc/heap-page.h"
#include "test/unittests/heap/cppgc/tests.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace cppgc {
namespace internal {

namespace {

class GCedBase : public GarbageCollected<GCedBase> {
 public:
  virtual void Trace(cppgc::Visitor*) const {}
};

template <size_t Size>
class GCed : public GCedBase {
 public:
  virtual void Trace(cppgc::Visitor*) const {}

 private:
  char array_[Size];
};

class UnmarkerTest : public testing::TestSupportingAllocationOnly {
 public:
  void StartUnmarking() {
    Heap* heap = Heap::From(GetHeap());
    ResetLinearAllocationBuffers();
    heap->unmarker().Start(Unmarker::Config::kConcurrent);
  }

  void FinishUnmarking() { Heap::From(GetHeap())->unmarker().Finish(); }

  void MarkObject(void* payload) {
    HeapObjectHeader& header = HeapObjectHeader::FromPayload(payload);
    header.TryMarkAtomic();
  }
};

std::vector<GCedBase*> AllocateBunch(
    AllocationHandle& handle) {  // NOLINT(runtime/references)
  const std::vector<GCedBase*> vec{
      MakeGarbageCollected<GCed<1>>(handle),
      MakeGarbageCollected<GCed<32>>(handle),
      MakeGarbageCollected<GCed<64>>(handle),
      MakeGarbageCollected<GCed<128>>(handle),
      MakeGarbageCollected<GCed<2 * kLargeObjectSizeThreshold>>(handle)};

  std::vector<GCedBase*> result;
  for (size_t i = 0; i < 100; ++i) {
    std::copy(vec.begin(), vec.end(), std::back_inserter(result));
  }

  return result;
}

}  // namespace

TEST_F(UnmarkerTest, BackgroundUnmarking) {
  std::vector<GCedBase*> gceds = AllocateBunch(GetAllocationHandle());

  for (auto* gced : gceds) {
    HeapObjectHeader::FromPayload(gced).TryMarkAtomic();
  }

  StartUnmarking();

  for (size_t i = 0; i < 100; ++i) {
    AllocateBunch(GetAllocationHandle());
  }

  // Wait for concurrent unmarker to finish.
  GetPlatform().WaitAllBackgroundTasks();

  // Check that the objects were unmarked.
  for (auto* gced : gceds) {
    EXPECT_FALSE(HeapObjectHeader::FromPayload(gced).IsMarked());
  }

  FinishUnmarking();
}

}  // namespace internal
}  // namespace cppgc

#endif  // defined(CPPGC_YOUNG_GENERATION)
