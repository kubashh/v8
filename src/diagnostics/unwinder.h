// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_DIAGNOSTICS_UNWINDER_H_
#define V8_DIAGNOSTICS_UNWINDER_H_

#include <algorithm>

#include "include/v8.h"
#include "src/common/globals.h"
#include "src/execution/frame-constants.h"
#include "src/execution/pointer-authentication.h"

namespace v8 {

i::Address Load(i::Address address);

}  // namespace v8

#endif  // V8_DIAGNOSTICS_UNWINDER_H_
