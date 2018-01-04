// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_COMMON_UTILS_H_
#define V8_COMPILER_COMMON_UTILS_H_

#include "src/compiler/node.h"
#include "src/handles.h"
#include "src/objects/map.h"

namespace v8 {
namespace internal {
namespace compiler {

MaybeHandle<Map> GetMapWitness(Node* node);

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_COMMON_UTILS_H_
