// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/version.h"

#include "include/v8-version-string.h"

namespace v8 {
namespace internal {

const char* Version::version_string_ = V8_VERSION_STRING;

}  // namespace internal
}  // namespace v8
