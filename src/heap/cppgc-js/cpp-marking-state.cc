

// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/cppgc-js/cpp-marking-state.h"

#include "src/objects/js-objects.h"

namespace v8 {
namespace internal {

bool CppMarkingState::CheckJSObject(const JSObject& js_object) {
  return js_object.IsApiWrapper();
}

}  // namespace internal
}  // namespace v8
