// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test/cctest/cctest.h"
#include "tools/v8dbg/test/v8dbg-test.h"

namespace v8 {
namespace internal {

UNINITIALIZED_TEST(V8dbg) { v8dbg_test::RunTests(); }

}  // namespace internal
}  // namespace v8
