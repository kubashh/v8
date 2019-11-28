// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/heap.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace internal {

TEST(OutOfLineMemoryChunkHeaderMap, InsertAndRemoveEntriesLinearly) {
  int entries = 10;
  Heap::OutOfLineMemoryChunkHeaderMap map(entries);

  for (int i = 0; i < entries; i++) {
    map.RegisterNewMemoryChunk(reinterpret_cast<MemoryChunk*>(i));
    CHECK_EQ(map.page_header_data_[i].chunk, reinterpret_cast<MemoryChunk*>(i));
  }
  CHECK(map.IsFull());
  for (int i = 0; i < entries; i++) {
    map.Free(i);
    if (i) {
      CHECK_EQ(map.page_header_data_[i].chunk,
               reinterpret_cast<MemoryChunk*>(i - 1));
    } else {
      CHECK_EQ(map.page_header_data_[i].chunk,
               reinterpret_cast<MemoryChunk*>(entries));
    }
  }
  CHECK(!map.IsFull());
}

TEST(OutOfLineMemoryChunkHeaderMap, InsertAndRemoveReferenceEntriesLinearly) {
  int entries = 10;
  Heap::OutOfLineMemoryChunkHeaderMap map(entries);

  for (int i = 0; i < entries; i++) {
    map.RegisterNewMemoryChunk(reinterpret_cast<MemoryChunk*>(i));
    CHECK_EQ(map.page_header_data_[i].chunk, reinterpret_cast<MemoryChunk*>(i));
  }
  CHECK(map.IsFull());
  for (int i = 0; i < entries; i++) {
    map.Free(&map.page_header_data_[i]);
    if (i) {
      CHECK_EQ(map.page_header_data_[i].chunk,
               reinterpret_cast<MemoryChunk*>(i - 1));
    } else {
      CHECK_EQ(map.page_header_data_[i].chunk,
               reinterpret_cast<MemoryChunk*>(entries));
    }
  }
  CHECK(!map.IsFull());
}

TEST(OutOfLineMemoryChunkHeaderMap, InsertAndRemoveEntriesReverse) {
  int entries = 10;
  Heap::OutOfLineMemoryChunkHeaderMap map(entries);

  for (int i = 0; i < entries; i++) {
    map.RegisterNewMemoryChunk(reinterpret_cast<MemoryChunk*>(i));
    CHECK_EQ(map.page_header_data_[i].chunk, reinterpret_cast<MemoryChunk*>(i));
  }
  CHECK(map.IsFull());
  for (int i = entries - 1; i >= 0; i--) {
    map.Free(i);
    if (i == entries - 1) {
      CHECK_EQ(map.page_header_data_[i].chunk,
               reinterpret_cast<MemoryChunk*>(entries));
    } else {
      CHECK_EQ(map.page_header_data_[i].chunk,
               reinterpret_cast<MemoryChunk*>(i + 1));
    }
  }
  CHECK(!map.IsFull());
}

TEST(OutOfLineMemoryChunkHeaderMap, InsertAndRemoveReferenceEntriesReverse) {
  int entries = 10;
  Heap::OutOfLineMemoryChunkHeaderMap map(entries);

  for (int i = 0; i < entries; i++) {
    map.RegisterNewMemoryChunk(reinterpret_cast<MemoryChunk*>(i));
    CHECK_EQ(map.page_header_data_[i].chunk, reinterpret_cast<MemoryChunk*>(i));
  }
  CHECK(map.IsFull());
  for (int i = entries - 1; i >= 0; i--) {
    map.Free(&map.page_header_data_[i]);
    if (i == entries - 1) {
      CHECK_EQ(map.page_header_data_[i].chunk,
               reinterpret_cast<MemoryChunk*>(entries));
    } else {
      CHECK_EQ(map.page_header_data_[i].chunk,
               reinterpret_cast<MemoryChunk*>(i + 1));
    }
  }
  CHECK(!map.IsFull());
}

TEST(OutOfLineMemoryChunkHeaderMap, InsertAndRemoveEntriesLinearlyLoop) {
  int entries = 10;
  int iterations = 3;
  Heap::OutOfLineMemoryChunkHeaderMap map(entries);

  for (int j = 0; j < iterations; j++) {
    for (int i = 0; i < entries; i++) {
      map.RegisterNewMemoryChunk(reinterpret_cast<MemoryChunk*>(i));
    }
    CHECK(map.IsFull());
    for (int i = entries - 1; i >= 0; i--) {
      map.Free(i);
    }
    CHECK(!map.IsFull());
  }
}

TEST(OutOfLineMemoryChunkHeaderMap, InsertAndRemoveEntriesReverseLoop) {
  int entries = 10;
  int iterations = 3;
  Heap::OutOfLineMemoryChunkHeaderMap map(entries);

  for (int j = 0; j < iterations; j++) {
    for (int i = 0; i < entries; i++) {
      map.RegisterNewMemoryChunk(reinterpret_cast<MemoryChunk*>(i));
    }
    CHECK(map.IsFull());
    for (int i = entries - 1; i >= 0; i--) {
      map.Free(i);
    }
    CHECK(!map.IsFull());
  }
}

TEST(OutOfLineMemoryChunkHeaderMap, InsertAndRemoveEntriesWithHoles) {
  int entries = 10;
  int iterations = 3;
  Heap::OutOfLineMemoryChunkHeaderMap map(entries);

  for (int i = 0; i < entries; i++) {
    map.RegisterNewMemoryChunk(reinterpret_cast<MemoryChunk*>(i));
  }
  CHECK(map.IsFull());

  for (int j = 0; j < iterations; j++) {
    for (int i = 0; i < entries; i += 2) {
      map.Free(i);
    }
    for (int i = 0; i < entries; i += 2) {
      map.RegisterNewMemoryChunk(reinterpret_cast<MemoryChunk*>(i));
    }
    CHECK(map.IsFull());
  }
}

}  // namespace internal
}  // namespace v8
