// Copyright 2018 the V8 project authors. All rights reserved.
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
      : zone_(zone), refs_snapshot_(zone_) {}

  const ZoneUnorderedMap<Address, ObjectData*>& GetSnapshot() const {
    return refs_snapshot_;
  }
  void SetSnapshot(const ZoneUnorderedMap<Address, ObjectData*>& refs) {
    refs_snapshot_ = refs;
  }

  bool IsInitialized() const { return !refs_snapshot_.empty(); }

  Zone* zone() const { return zone_; }

 private:
  Zone* const zone_;

  ZoneUnorderedMap<Address, ObjectData*> refs_snapshot_;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_COMPILER_DATA_H_
