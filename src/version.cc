// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/version.h"

#include "include/v8-version.h"
#include "include/v8-version-string.h"

namespace v8 {
namespace internal {

const int Version::major_ = V8_MAJOR_VERSION;
const int Version::minor_ = V8_MINOR_VERSION;
const int Version::build_ = V8_BUILD_NUMBER;
const int Version::patch_ = V8_PATCH_LEVEL;
const bool Version::candidate_ = (V8_IS_CANDIDATE_VERSION != 0);
const char* Version::version_string_ = V8_VERSION_STRING;

}  // namespace internal
}  // namespace v8
