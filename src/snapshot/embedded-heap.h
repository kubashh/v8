// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SNAPSHOT_EMBEDDED_HEAP
#define V8_SNAPSHOT_EMBEDDED_HEAP

#include <vector>

#include "src/common/globals.h"
#include "src/utils/vector.h"

namespace v8 {
namespace internal {

class Isolate;
class ReadOnlyHeap;

enum class EmbeddedHeapSyncTag {};

class EmbeddedHeapWriter {
 public:
  EmbeddedHeapWriter(std::vector<byte>* sink) : sink_(sink) {}
  void WriteHeader(ReadOnlyHeap* ro_heap);
  void WriteValue(Tagged_t value);

 private:
  std::vector<byte>* sink_;
};

class EmbeddedHeapReader {
 public:
  EmbeddedHeapReader(Vector<const byte> source) : source_(source) {}
  void ReadHeader(Isolate* ro_heap);
  Tagged_t ReadValue();

  int location() const { return location_; }

 private:
  byte ReadByte() { return source_[location_++]; }

  int location_ = 0;
  Vector<const byte> source_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_SNAPSHOT_EMBEDDED_HEAP
