// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/codegen/tick-counter.h"

#include "src/base/logging.h"
#include "src/base/macros.h"

namespace v8 {
namespace internal {

void TickCounter::DoTick() {
  ++ticks_;
  // Magical number to detect performance bugs or compiler divergence.
  // Chosen to be above the 99.9999th percentile according to UMA.
  constexpr size_t kMaxTicks = 5000000;
  USE(kMaxTicks);
  DCHECK_LT(ticks_, kMaxTicks);
}

}  // namespace internal
}  // namespace v8
