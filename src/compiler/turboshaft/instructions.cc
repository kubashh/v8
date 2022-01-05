// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/turboshaft/instructions.h"

namespace v8 {
namespace internal {
namespace compiler {
namespace turboshaft {

#define ASSERT_INSTRUCTION_IS_INLINE(Name) \
  STATIC_ASSERT((!std::is_base_of<OutOfLineInstr, Name##Instr>::value));
INLINE_INSTRUCTION_LIST(ASSERT_INSTRUCTION_IS_INLINE)
#undef ASSERT_INSTRUCTION_IS_INLINE

#define ASSERT_INSTRUCTION_IS_OUT_OF_LINE(Name) \
  STATIC_ASSERT((std::is_base_of<OutOfLineInstr, Name##Instr>::value));
OUT_OF_LINE_INSTRUCTION_LIST(ASSERT_INSTRUCTION_IS_OUT_OF_LINE)
#undef ASSERT_INSTRUCTION_IS_OUT_OF_LINE

}  // namespace turboshaft
}  // namespace compiler
}  // namespace internal
}  // namespace v8
