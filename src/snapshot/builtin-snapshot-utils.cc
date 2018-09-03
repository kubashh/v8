// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/snapshot/builtin-snapshot-utils.h"

namespace v8 {
namespace internal {

// static
bool BuiltinSnapshotUtils::IsBuiltinIndex(int maybe_index) {
  return (kFirstBuiltinIndex <= maybe_index &&
          maybe_index < kFirstBuiltinIndex + kNumberOfBuiltins);
}

}  // namespace internal
}  // namespace v8
