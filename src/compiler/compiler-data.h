// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_COMPILER_DATA_H_
#define V8_COMPILER_COMPILER_DATA_H_

#include "src/zone/zone-containers.h"

namespace v8 {
namespace internal {

class Isolate;
class Zone;

namespace compiler {

class ObjectData;

// This class serves as a per-isolate container of data that should be
// persisted between compiler runs. For now it stores the code builtins
// so they are not serialized on each compiler run.
class CompilerData : public ZoneObject {
 public:
  CompilerData(Isolate* isolate, Zone* zone)
      : zone_(zone), initialized_(false), refs_snapshot_(zone_, 100) {}

  const ZoneUnorderedMap<Address, ObjectData*>& GetSnapshot() const {
    return refs_snapshot_;
  }
  void SetSnapshot(const ZoneUnorderedMap<Address, ObjectData*>& refs) {
    refs_snapshot_ = refs;
    initialized_ = true;
  }

  bool IsInitialized() const { return initialized_; }

  Zone* zone() const { return zone_; }

 private:
  Zone* zone_;
  bool initialized_;

  ZoneUnorderedMap<Address, ObjectData*> refs_snapshot_;

  // TODO(mslekova): Move this to a common place with the broker if we increase
  // it
  static const size_t kInitialRefsBucketCount = 1000;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_COMPILER_DATA_H_
