// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/base/bits.h"
#include "src/execution/arguments-inl.h"
#include "src/execution/isolate-inl.h"
#include "src/init/bootstrapper.h"
#include "src/logging/counters.h"
#include "src/objects/js-temporal-objects.h"
#include "src/runtime/runtime-utils.h"

namespace v8 {
namespace internal {

RUNTIME_FUNCTION(Runtime_IsValidTemporalCalendarField) {
  SealHandleScope shs(isolate);
  DCHECK_EQ(2, args.length());
  CONVERT_ARG_HANDLE_CHECKED(String, s, 0);
  CONVERT_ARG_HANDLE_CHECKED(FixedArray, f, 1);
  RETURN_RESULT_OR_FAILURE(
      isolate, temporal::IsValidTemporalCalendarField(isolate, s, f));
}

}  // namespace internal
}  // namespace v8
