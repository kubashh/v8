// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "include/cppgc/allocation.h"
#include "include/cppgc/custom-space.h"
#include "include/cppgc/persistent.h"
#include "src/base/macros.h"
#include "src/heap/cppgc/sanitizers.h"
#include "test/unittests/heap/cppgc/tests.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace cppgc {
namespace internal {

namespace {

class CustomSpaceWithAsanAnnotations
    : public CustomSpace<CustomSpaceWithAsanAnnotations> {
 public:
  static constexpr size_t kSpaceIndex = 0;
  static constexpr bool kNeedsAsanContiguousContainerAnnotations = true;
};

}  // namespace

#ifdef V8_USE_ADDRESS_SANITIZER

namespace {

class AsanContiguousContainerTest : public testing::TestWithPlatform {
 protected:
  AsanContiguousContainerTest() {
    Heap::HeapOptions options;
    options.custom_spaces.emplace_back(
        std::make_unique<CustomSpaceWithAsanAnnotations>());
    heap_ = Heap::Create(platform_, std::move(options));
  }

  void PreciseGC() {
    heap_->ForceGarbageCollectionSlow(
        ::testing::UnitTest::GetInstance()->current_test_info()->name(),
        "Testing", cppgc::Heap::StackState::kNoHeapPointers);
  }

  cppgc::Heap* GetHeap() const { return heap_.get(); }

 private:
  std::unique_ptr<cppgc::Heap> heap_;
};

struct Dummy {};

class ObjectWithAsanAnnotation final
    : public GarbageCollected<ObjectWithAsanAnnotation> {
 public:
  ObjectWithAsanAnnotation() : dummy_(std::make_unique<Dummy>()) {
    // Forbid any access to this object.
    ASAN_ANNOTATE_CONTIGUOUS_CONTAINER(this, 1, 1, 0);
  }

  void Trace(Visitor* visitor) const { visitor->Trace(next_); }

  Member<ObjectWithAsanAnnotation>& next_ref() { return next_; }

 private:
  std::unique_ptr<Dummy> dummy_;
  Member<ObjectWithAsanAnnotation> next_;
};

}  // namespace

}  // namespace internal

template <>
struct SpaceTrait<internal::ObjectWithAsanAnnotation> {
  using Space = internal::CustomSpaceWithAsanAnnotations;
};

namespace internal {

TEST_F(AsanContiguousContainerTest, CustomSpaceMarkedWithNeedsAnnotation) {
  EXPECT_TRUE(std::make_unique<CustomSpaceWithAsanAnnotations>()
                  ->NeedsAsanContiguousContainerAnnotations());
}

TEST_F(AsanContiguousContainerTest, ObjectDestruction) {
  MakeGarbageCollected<ObjectWithAsanAnnotation>(
      GetHeap()->GetAllocationHandle());
  PreciseGC();
}

TEST_F(AsanContiguousContainerTest, RegularMarking) {
  Persistent<ObjectWithAsanAnnotation> holder{
      MakeGarbageCollected<ObjectWithAsanAnnotation>(
          GetHeap()->GetAllocationHandle())};
  PreciseGC();
}

TEST_F(AsanContiguousContainerTest, IncrementalMarkingWriteBarrier) {
  Persistent<ObjectWithAsanAnnotation> holder{
      MakeGarbageCollected<ObjectWithAsanAnnotation>(
          GetHeap()->GetAllocationHandle())};
  using Config = Heap::Config;
  static constexpr Config IncrementalPreciseConfig = {
      Config::CollectionType::kMajor, Config::StackState::kNoHeapPointers,
      Config::MarkingType::kIncremental, Config::SweepingType::kAtomic};
  Heap* heap = Heap::From(GetHeap());
  heap->StartIncrementalGarbageCollection(IncrementalPreciseConfig);
  holder->next_ref() = MakeGarbageCollected<ObjectWithAsanAnnotation>(
      GetHeap()->GetAllocationHandle());
  heap->FinalizeIncrementalGarbageCollectionIfRunning(IncrementalPreciseConfig);
}

TEST_F(AsanContiguousContainerTest, ConcurrentMarking) {
  Persistent<ObjectWithAsanAnnotation> holder{
      MakeGarbageCollected<ObjectWithAsanAnnotation>(
          GetHeap()->GetAllocationHandle())};
  using Config = Heap::Config;
  static constexpr Config ConcurrentPreciseConfig = {
      Config::CollectionType::kMajor, Config::StackState::kNoHeapPointers,
      Config::MarkingType::kIncrementalAndConcurrent,
      Config::SweepingType::kAtomic};
  Heap* heap = Heap::From(GetHeap());
  heap->StartIncrementalGarbageCollection(ConcurrentPreciseConfig);
  auto* marker = heap->marker();
  marker->SetMainThreadMarkingDisabledForTesting(true);
  marker->IncrementalMarkingStepForTesting(Config::StackState::kNoHeapPointers);
  marker->WaitForConcurrentMarkingForTesting();
  marker->SetMainThreadMarkingDisabledForTesting(false);
  heap->FinalizeIncrementalGarbageCollectionIfRunning(ConcurrentPreciseConfig);
}

#else  // !V8_USE_ADDRESS_SANITIZER

using AsanContiguousContainerTest = testing::TestWithPlatform;

TEST_F(AsanContiguousContainerTest, SettingFlagOnNonAsanCrashes) {
  Heap::HeapOptions options;
  options.custom_spaces.emplace_back(
      std::make_unique<CustomSpaceWithAsanAnnotations>());
  EXPECT_DEATH_IF_SUPPORTED(Heap::Create(platform_, std::move(options)), "");
}

#endif  // !V8_USE_ADDRESS_SANITIZER

}  // namespace internal
}  // namespace cppgc
