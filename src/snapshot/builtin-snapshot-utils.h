// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SNAPSHOT_BUILTIN_SNAPSHOT_UTILS_H_
#define V8_SNAPSHOT_BUILTIN_SNAPSHOT_UTILS_H_

#include "src/builtins/builtins.h"
#include "src/globals.h"

namespace v8 {
namespace internal {

// Constants and utility methods used by builtin (de)serialization.
class BuiltinSnapshotUtils : public AllStatic {
 public:
  static const int kFirstBuiltinIndex = 0;
  static const int kNumberOfBuiltins = Builtins::builtin_count;
  static const int kNumberOfCodeObjects = kNumberOfBuiltins;

  // Indexes into the offsets vector contained in snapshot.
  // See e.g. BuiltinSerializer::code_offsets_.
  static bool IsBuiltinIndex(int maybe_index);
};

}  // namespace internal
}  // namespace v8

#endif  // V8_SNAPSHOT_BUILTIN_SNAPSHOT_UTILS_H_
