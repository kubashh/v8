// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/turboshaft/operations.h"

#include <sstream>

namespace v8 {
namespace internal {
namespace compiler {
namespace turboshaft {

#define ASSERT_OPERATION_IS_INLINE(Name) \
  STATIC_ASSERT((!std::is_base_of<OutOfLineOp, Name##Op>::value));
TURBOSHAFT_INLINE_OPERATION_LIST(ASSERT_OPERATION_IS_INLINE)
#undef ASSERT_OPERATION_IS_INLINE

#define ASSERT_OPERATION_IS_OUT_OF_LINE(Name) \
  STATIC_ASSERT((std::is_base_of<OutOfLineOp, Name##Op>::value));
TURBOSHAFT_OUT_OF_LINE_OPERATION_LIST(ASSERT_OPERATION_IS_OUT_OF_LINE)
#undef ASSERT_OPERATION_IS_OUT_OF_LINE

const char* OpcodeName(Opcode opcode) {
#define OPCODE_NAME(Name) #Name,
  const char* table[kNumberOfOpcodes] = {
      TURBOSHAFT_OPERATION_LIST(OPCODE_NAME)};
#undef OPCODE_NAME
  return table[ToUnderlyingType(opcode)];
}

std::ostream& operator<<(std::ostream& os, const Operation& op) {
  // TODO(tebbi): Also print operation options.
  os << OpcodeName(op.opcode) << "(";
  bool first = true;
  for (OpIndex input : op.Inputs()) {
    if (!first) os << ", ";
    first = false;
    os << "#" << ToUnderlyingType(input);
  }
  return os << ")";
}

std::string Operation::ToString() const {
  std::stringstream ss;
  ss << *this;
  return ss.str();
}

}  // namespace turboshaft
}  // namespace compiler
}  // namespace internal
}  // namespace v8
