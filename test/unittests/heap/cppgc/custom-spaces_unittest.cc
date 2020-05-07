// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/cppgc/allocation.h"
#include "src/heap/cppgc/heap-page.h"
#include "src/heap/cppgc/raw-heap.h"
#include "test/unittests/heap/cppgc/tests.h"

namespace cppgc {
namespace internal {

namespace {

size_t g_destructor_callcount;

class TestWithHeapWithCustomSpaces : public testing::TestWithPlatform {
 public:
  static constexpr size_t kNumberOfCustomSpaces = 2;
  static constexpr size_t kCustomSpaceIndex1 = 0;
  static constexpr size_t kCustomSpaceIndex2 = 1;

 protected:
  TestWithHeapWithCustomSpaces()
      : heap_(Heap::Create({kNumberOfCustomSpaces})) {
    g_destructor_callcount = 0;
  }

  void PreciseGC() {
    heap_->ForceGarbageCollectionSlow("TestWithHeap", "Testing",
                                      Heap::GCConfig::StackState::kEmpty);
  }

  cppgc::Heap* GetHeap() const { return heap_.get(); }

 private:
  std::unique_ptr<cppgc::Heap> heap_;
};

class RegularGCed final : public GarbageCollected<RegularGCed> {};

class CustomGCed1 final : public GarbageCollected<CustomGCed1> {
 public:
  ~CustomGCed1() { g_destructor_callcount++; }
};
class CustomGCed2 final : public GarbageCollected<CustomGCed2> {
 public:
  ~CustomGCed2() { g_destructor_callcount++; }
};

class CustomGCedBase : public GarbageCollected<CustomGCedBase> {};
class CustomGCedFinal1 final : public CustomGCedBase {
 public:
  ~CustomGCedFinal1() { g_destructor_callcount++; }
};
class CustomGCedFinal2 final : public CustomGCedBase {
 public:
  ~CustomGCedFinal2() { g_destructor_callcount++; }
};

}  // namespace

}  // namespace internal

template <>
struct SpacePolicyTrait<internal::CustomGCed1> {
  static constexpr Heap::SpacePolicy kSpacePolicy = Heap::SpacePolicy::kCustom;
  static constexpr size_t kSpaceIndex =
      internal::TestWithHeapWithCustomSpaces::kCustomSpaceIndex1;
};

template <>
struct SpacePolicyTrait<internal::CustomGCed2> {
  static constexpr Heap::SpacePolicy kSpacePolicy = Heap::SpacePolicy::kCustom;
  static constexpr size_t kSpaceIndex =
      internal::TestWithHeapWithCustomSpaces::kCustomSpaceIndex2;
};

template <typename T>
struct SpacePolicyTrait<
    T, std::enable_if_t<std::is_base_of<internal::CustomGCedBase, T>::value>> {
  static constexpr Heap::SpacePolicy kSpacePolicy = Heap::SpacePolicy::kCustom;
  static constexpr size_t kSpaceIndex =
      internal::TestWithHeapWithCustomSpaces::kCustomSpaceIndex1;
};

namespace internal {

TEST_F(TestWithHeapWithCustomSpaces, AllocateOnCustomSpaces) {
  auto* regular = MakeGarbageCollected<RegularGCed>(GetHeap());
  auto* custom1 = MakeGarbageCollected<CustomGCed1>(GetHeap());
  auto* custom2 = MakeGarbageCollected<CustomGCed2>(GetHeap());
  EXPECT_EQ(RawHeap::kNumberOfRegularSpaces,
            NormalPage::FromPayload(custom1)->space()->index());
  EXPECT_EQ(RawHeap::kNumberOfRegularSpaces + 1,
            NormalPage::FromPayload(custom2)->space()->index());
  EXPECT_EQ(static_cast<size_t>(RawHeap::RegularSpaceType::kNormal1),
            NormalPage::FromPayload(regular)->space()->index());
}

TEST_F(TestWithHeapWithCustomSpaces,
       AllocateOnCustomSpacesSpecifiedThroughBase) {
  auto* regular = MakeGarbageCollected<RegularGCed>(GetHeap());
  auto* custom1 = MakeGarbageCollected<CustomGCedFinal1>(GetHeap());
  auto* custom2 = MakeGarbageCollected<CustomGCedFinal2>(GetHeap());
  EXPECT_EQ(RawHeap::kNumberOfRegularSpaces,
            NormalPage::FromPayload(custom1)->space()->index());
  EXPECT_EQ(RawHeap::kNumberOfRegularSpaces,
            NormalPage::FromPayload(custom2)->space()->index());
  EXPECT_EQ(static_cast<size_t>(RawHeap::RegularSpaceType::kNormal1),
            NormalPage::FromPayload(regular)->space()->index());
}

TEST_F(TestWithHeapWithCustomSpaces, SweepCustomSpace) {
  MakeGarbageCollected<CustomGCedFinal1>(GetHeap());
  MakeGarbageCollected<CustomGCedFinal2>(GetHeap());
  MakeGarbageCollected<CustomGCed1>(GetHeap());
  MakeGarbageCollected<CustomGCed2>(GetHeap());
  EXPECT_EQ(0u, g_destructor_callcount);
  PreciseGC();
  EXPECT_EQ(4u, g_destructor_callcount);
}

}  // namespace internal
}  // namespace cppgc
