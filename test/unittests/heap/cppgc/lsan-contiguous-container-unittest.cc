// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "include/cppgc/allocation.h"
#include "include/cppgc/custom-space.h"
#include "include/cppgc/persistent.h"
#include "src/heap/cppgc/sanitizers.h"
#include "test/unittests/heap/cppgc/tests.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace cppgc {
namespace internal {

namespace {

class CustomSpaceWithLSANAnnotations
    : public CustomSpace<CustomSpaceWithLSANAnnotations> {
 public:
  static constexpr size_t kSpaceIndex = 0;
  static constexpr bool kNeedsLSANContiguousContainerAnnotations = true;
};

class LSANContiguousContainerTest : public testing::TestWithPlatform {
 protected:
  LSANContiguousContainerTest() {
    Heap::HeapOptions options;
    options.custom_spaces.emplace_back(
        std::make_unique<CustomSpaceWithLSANAnnotations>());
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

class ObjectWithLSANAnnotation final
    : public GarbageCollected<ObjectWithLSANAnnotation> {
 public:
  ObjectWithLSANAnnotation() : dummy_(std::make_unique<Dummy>()) {
    // Forbid any access to this object.
    LSAN_ANNOTATE_CONTIGUOUS_CONTAINER(this, 1, 1, 0);
  }

  void Trace(Visitor* visitor) const { visitor->Trace(next_); }

  Member<ObjectWithLSANAnnotation>& next_ref() { return next_; }

 private:
  std::unique_ptr<Dummy> dummy_;
  Member<ObjectWithLSANAnnotation> next_;
};

}  // namespace

}  // namespace internal

template <>
struct SpaceTrait<internal::ObjectWithLSANAnnotation> {
  using Space = internal::CustomSpaceWithLSANAnnotations;
};

namespace internal {

TEST_F(LSANContiguousContainerTest, CustomSpaceMarkedWithNeedsAnnotation) {
  EXPECT_TRUE(std::make_unique<CustomSpaceWithLSANAnnotations>()
                  ->NeedsLSANContiguousContainerAnnotations());
}

TEST_F(LSANContiguousContainerTest, ObjectDestruction) {
  MakeGarbageCollected<ObjectWithLSANAnnotation>(
      GetHeap()->GetAllocationHandle());
  PreciseGC();
}

TEST_F(LSANContiguousContainerTest, RegularMarking) {
  Persistent<ObjectWithLSANAnnotation> holder{
      MakeGarbageCollected<ObjectWithLSANAnnotation>(
          GetHeap()->GetAllocationHandle())};
  PreciseGC();
}

TEST_F(LSANContiguousContainerTest, IncrementalMarkingWriteBarrier) {
  Persistent<ObjectWithLSANAnnotation> holder{
      MakeGarbageCollected<ObjectWithLSANAnnotation>(
          GetHeap()->GetAllocationHandle())};
  using Config = Heap::Config;
  static constexpr Config IncrementalPreciseConfig = {
      Config::CollectionType::kMajor, Config::StackState::kNoHeapPointers,
      Config::MarkingType::kIncremental, Config::SweepingType::kAtomic};
  Heap* heap = Heap::From(GetHeap());
  heap->StartIncrementalGarbageCollection(IncrementalPreciseConfig);
  holder->next_ref() = MakeGarbageCollected<ObjectWithLSANAnnotation>(
      GetHeap()->GetAllocationHandle());
  heap->FinalizeIncrementalGarbageCollectionIfRunning(IncrementalPreciseConfig);
}

TEST_F(LSANContiguousContainerTest, ConcurrentMarking) {
  Persistent<ObjectWithLSANAnnotation> holder{
      MakeGarbageCollected<ObjectWithLSANAnnotation>(
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

}  // namespace internal

}  // namespace cppgc
