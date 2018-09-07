// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdio>

#include "src/base/platform/platform.h"
#include "src/interpreter/bytecodes.h"

namespace v8 {
namespace internal {
namespace interpreter {

void WriteEnumValue(FILE* fp, Bytecode bytecode, OperandScale operand_scale) {
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

    fprintf(fp,
            " \\\n  V(%sHandler, interpreter::Bytecode::k%s, "
            "interpreter::OperandScale::k%s)",
            Bytecodes::ToString(bytecode, operand_scale, "").c_str(),
            Bytecodes::ToString(bytecode), scale_string);
  }
}

void WriteHeader(const char* header_filename) {
  FILE* fp = v8::base::OS::FOpen(header_filename, "w");

  fprintf(fp, "#define FLAT_BYTECODE_LIST(V)");
#define ADD_BYTECODES(Name, ...) \
  WriteEnumValue(fp, Bytecode::k##Name, operand_scale);
  OperandScale operand_scale = OperandScale::kSingle;
  BYTECODE_LIST(ADD_BYTECODES)
  operand_scale = OperandScale::kDouble;
  BYTECODE_LIST(ADD_BYTECODES)
  operand_scale = OperandScale::kQuadruple;
  BYTECODE_LIST(ADD_BYTECODES)
#undef ADD_BYTECODES
  fprintf(fp, "\n");

  fclose(fp);
}

}  // namespace interpreter
}  // namespace internal
}  // namespace v8

int main(int argc, const char* argv[]) {
  v8::base::EnsureConsoleOutput();

  CHECK_EQ(argc, 2);

  v8::internal::interpreter::WriteHeader(argv[1]);

  return 0;
}
