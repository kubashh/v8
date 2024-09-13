// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BUILTINS_BUILTINS_DECORATORS_H_
#define V8_BUILTINS_BUILTINS_DECORATORS_H_

#include "src/objects/contexts.h"

namespace v8 {
namespace internal {

class DecoratorsBuiltins {
 public:
  enum AddInitializerContextSlots {
    kExtraInitializersContextSlot = Context::MIN_CONTEXT_SLOTS,
    kAddInitializerContextLength,
  };

  enum DecoratorAccessContextSlots {
    kNameContextSlot = Context::MIN_CONTEXT_SLOTS,
    kDecoratorAccessContextLength,
  };
};

}  // namespace internal
}  // namespace v8

#endif  // V8_BUILTINS_BUILTINS_DECORATORS_H_
