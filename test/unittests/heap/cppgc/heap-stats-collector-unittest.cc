// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/cppgc/heap-stats-collector.h"

#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace cppgc {
namespace internal {

namespace {

constexpr size_t kNoMarkedBytes = 0;

constexpr size_t kMinReportedSize =
    HeapStatsCollector::kAllocationThresholdBytes;

class HeapStatsCollectorTest : public ::testing::Test {
 public:
  void FakeAllocate(size_t bytes) {
    stats.IncreaseAllocatedObjectSize(bytes);
    stats.AllocatedObjectSizeSafepoint();
  }

  void FakeFree(size_t bytes) {
    stats.DecreaseAllocatedObjectSize(bytes);
    stats.AllocatedObjectSizeSafepoint();
  }

  HeapStatsCollector stats;
};

}  // namespace

TEST_F(HeapStatsCollectorTest, NoMarkedBytes) {
  stats.NotifyMarkingStarted();
  stats.NotifyMarkingCompleted(kNoMarkedBytes);
  auto event = stats.NotifySweepingCompleted();
  EXPECT_EQ(0u, event.marked_bytes);
}

TEST_F(HeapStatsCollectorTest, EventPrevGCMarkedObjectSize) {
  stats.NotifyMarkingStarted();
  stats.NotifyMarkingCompleted(1024);
  auto event = stats.NotifySweepingCompleted();
  EXPECT_EQ(1024u, event.marked_bytes);
}

TEST_F(HeapStatsCollectorTest,
       AllocationNoReportBelowAllocationThresholdBytes) {
  constexpr size_t kObjectSize = 17;
  EXPECT_LT(kObjectSize, HeapStatsCollector::kAllocationThresholdBytes);
  FakeAllocate(kObjectSize);
  EXPECT_EQ(0u, stats.allocated_object_size());
}

TEST_F(HeapStatsCollectorTest, AlllocationReportAboveAllocationThresholdBytes) {
  constexpr size_t kObjectSize = HeapStatsCollector::kAllocationThresholdBytes;
  EXPECT_GE(kObjectSize, HeapStatsCollector::kAllocationThresholdBytes);
  FakeAllocate(kObjectSize);
  EXPECT_EQ(kObjectSize, stats.allocated_object_size());
}

TEST_F(HeapStatsCollectorTest, InitialAllocatedObjectSize) {
  stats.NotifyMarkingStarted();
  EXPECT_EQ(0u, stats.allocated_object_size());
  stats.NotifyMarkingCompleted(kNoMarkedBytes);
  EXPECT_EQ(0u, stats.allocated_object_size());
  stats.NotifySweepingCompleted();
  EXPECT_EQ(0u, stats.allocated_object_size());
}

TEST_F(HeapStatsCollectorTest, AllocatedObjectSize) {
  stats.NotifyMarkingStarted();
  FakeAllocate(kMinReportedSize);
  EXPECT_EQ(kMinReportedSize, stats.allocated_object_size());
  stats.NotifyMarkingCompleted(kMinReportedSize);
  EXPECT_EQ(kMinReportedSize, stats.allocated_object_size());
  stats.NotifySweepingCompleted();
  EXPECT_EQ(kMinReportedSize, stats.allocated_object_size());
}

TEST_F(HeapStatsCollectorTest, AllocatedObjectSizeNoMarkedBytes) {
  stats.NotifyMarkingStarted();
  FakeAllocate(kMinReportedSize);
  EXPECT_EQ(kMinReportedSize, stats.allocated_object_size());
  stats.NotifyMarkingCompleted(kNoMarkedBytes);
  EXPECT_EQ(0u, stats.allocated_object_size());
  stats.NotifySweepingCompleted();
  EXPECT_EQ(0u, stats.allocated_object_size());
}

TEST_F(HeapStatsCollectorTest, AllocatedObjectSizeAllocateAfterMarking) {
  stats.NotifyMarkingStarted();
  FakeAllocate(kMinReportedSize);
  EXPECT_EQ(kMinReportedSize, stats.allocated_object_size());
  stats.NotifyMarkingCompleted(kMinReportedSize);
  FakeAllocate(kMinReportedSize);
  EXPECT_EQ(2 * kMinReportedSize, stats.allocated_object_size());
  stats.NotifySweepingCompleted();
  EXPECT_EQ(2 * kMinReportedSize, stats.allocated_object_size());
}

class MockAllocationObserver : public HeapStatsCollector::AllocationObserver {
 public:
  MOCK_METHOD1(AllocatedObjectSizeIncreased, void(size_t));
  MOCK_METHOD1(AllocatedObjectSizeDecreased, void(size_t));
};

TEST_F(HeapStatsCollectorTest, RegisterUnregisterObserver) {
  MockAllocationObserver observer;
  stats.RegisterObserver(&observer);
  stats.UnregisterObserver(&observer);
}

TEST_F(HeapStatsCollectorTest, ObserveAllocatedObjectSize) {
  MockAllocationObserver observer;
  stats.RegisterObserver(&observer);
  EXPECT_CALL(observer, AllocatedObjectSizeIncreased(kMinReportedSize));
  FakeAllocate(kMinReportedSize);
  EXPECT_CALL(observer, AllocatedObjectSizeDecreased(kMinReportedSize));
  FakeFree(kMinReportedSize);
  stats.UnregisterObserver(&observer);
}

namespace {

void FakeGC(HeapStatsCollector* stats, size_t marked_bytes) {
  stats->NotifyMarkingStarted();
  stats->NotifyMarkingCompleted(marked_bytes);
  stats->NotifySweepingCompleted();
}

class AllocationObserverTriggeringGC final
    : public HeapStatsCollector::AllocationObserver {
 public:
  explicit AllocationObserverTriggeringGC(HeapStatsCollector* stats)
      : stats(stats) {}

  void AllocatedObjectSizeIncreased(size_t bytes) final {
    increase_call_count++;
    increased_size_bytes += bytes;
    if (increase_call_count == 1) {
      FakeGC(stats, bytes);
    }
  }

  // // Mock out the rest to trigger warnings if used.
  MOCK_METHOD1(AllocatedObjectSizeDecreased, void(size_t));

  size_t increase_call_count = 0;
  size_t increased_size_bytes = 0;
  HeapStatsCollector* stats;
};

}  // namespace

TEST_F(HeapStatsCollectorTest, ObserverTriggersGC) {
  AllocationObserverTriggeringGC gc_observer(&stats);
  MockAllocationObserver mock_observer;
  // // Internal detail: First registered observer is also notified first.
  stats.RegisterObserver(&gc_observer);
  stats.RegisterObserver(&mock_observer);

  // Since the GC clears counters, it should see an increase call with a delta
  // of zero bytes.
  EXPECT_CALL(mock_observer, AllocatedObjectSizeIncreased(0));

  // Trigger scenario.
  FakeAllocate(kMinReportedSize);

  EXPECT_EQ(1u, gc_observer.increase_call_count);
  EXPECT_EQ(kMinReportedSize, gc_observer.increased_size_bytes);

  stats.UnregisterObserver(&gc_observer);
  stats.UnregisterObserver(&mock_observer);
}

}  // namespace internal
}  // namespace cppgc
