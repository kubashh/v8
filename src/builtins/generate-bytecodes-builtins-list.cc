// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fstream>

#include "src/interpreter/bytecodes.h"

namespace v8 {
namespace internal {
namespace interpreter {

void WriteBytecode(std::ofstream& out, Bytecode bytecode,
                   OperandScale operand_scale) {
  if (Bytecodes::BytecodeHasHandler(bytecode, operand_scale)) {
    const char* scale_string;
    switch (operand_scale) {
#define SCALE_CASE(Name, ...) \
  case OperandScale::k##Name: \
    scale_string = #Name;     \
    break;
      OPERAND_SCALE_LIST(SCALE_CASE)
#undef SCALE_CASE
      default:
        UNREACHABLE();
    }

    out << " \\\n  V(" << Bytecodes::ToString(bytecode, operand_scale, "")
        << "Handler, interpreter::Bytecode::k" << Bytecodes::ToString(bytecode)
        << ", interpreter::OperandScale::k" << operand_scale << ")";
  }
}

void WriteHeader(const char* header_filename) {
  std::ofstream out(header_filename);

  out << "// Automatically generated from interpreter/bytecodes.h\n"
      << "// The following list macro is used to populate the builtins list\n"
      << "// with the bytecode handlers\n\n"
      << "#define BUILTIN_LIST_BYTECODE_HANDLERS(V)";
#ifdef V8_EMBEDDED_BUILTINS
#define ADD_BYTECODES(Name, ...) \
  WriteBytecode(out, Bytecode::k##Name, operand_scale);
  OperandScale operand_scale = OperandScale::kSingle;
  BYTECODE_LIST(ADD_BYTECODES)
  operand_scale = OperandScale::kDouble;
  BYTECODE_LIST(ADD_BYTECODES)
  operand_scale = OperandScale::kQuadruple;
  BYTECODE_LIST(ADD_BYTECODES)
#undef ADD_BYTECODES
#endif
  out << "\n";
}

}  // namespace interpreter
}  // namespace internal
}  // namespace v8

int main(int argc, const char* argv[]) {
  CHECK_EQ(argc, 2);

  v8::internal::interpreter::WriteHeader(argv[1]);

  return 0;
}
