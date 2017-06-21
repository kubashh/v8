// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/version.h"

#include "include/v8-version-string.h"

// Define SONAME to have the build system put a specific SONAME into the
// shared library instead the generic SONAME generated from the V8 version
// number. This define is mainly used by the build system script.
#define SONAME            ""

namespace v8 {
namespace internal {

template <>
const char* Version::soname_ = SONAME;
template <>
const char* Version::version_string_ = V8_VERSION_STRING;

}  // namespace internal
}  // namespace v8
