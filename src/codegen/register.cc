// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/codegen/register.h"

#include "src/codegen/register-arch.h"
#include "src/common/globals.h"

namespace v8 {
namespace internal {

int ArgumentPaddingSlots(int argument_count) {
  if (kStackFrameAlignment == kSystemPointerSize) return 0;
  constexpr int alignment_mask = kStackFrameAlignment / kSystemPointerSize - 1;
  return argument_count & alignment_mask;
}

}  // namespace internal
}  // namespace v8
