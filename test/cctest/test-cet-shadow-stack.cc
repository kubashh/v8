// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/v8-external.h"
#include "include/v8-function.h"
#include "include/v8-isolate.h"
#include "include/v8-local-handle.h"
#include "include/v8-template.h"
#include "src/base/win32-headers.h"
#include "src/init/v8.h"
#include "test/cctest/cctest.h"

namespace v8 {
namespace internal {

#ifdef V8_ENABLE_CET_SHADOW_STACK

void* return_address;

// Bug() simulates a ROP. The first time we are called we save the
// address we will return to and return to it (like a normal function
// call). The second time we return to the saved address. If called
// from a different function the second time, this redirects control
// flow and should be different from the return address in the shadow
// stack.
V8_NOINLINE void Bug() {
  void* pvAddressOfReturnAddress = _AddressOfReturnAddress();
  if (!return_address) {
    return_address = *reinterpret_cast<void**>(pvAddressOfReturnAddress);
  } else {
    *reinterpret_cast<void**>(pvAddressOfReturnAddress) = return_address;
  }
}

V8_NOINLINE void A() { Bug(); }

V8_NOINLINE void B() { Bug(); }

UNINITIALIZED_TEST(CETShadowStack) {
  if (base::OS::IsHardwareEnforcedShadowStacksEnabled()) {
    A();
    // TODO(mvstanton): Mark that B() should fail with an uncatchable exception.
    B();
  }
}

#endif  // V8_ENABLE_CET_SHADOW_STACK

}  // namespace internal
}  // namespace v8
