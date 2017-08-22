// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/interpreter/bytecode-register.h"

namespace v8 {
namespace internal {
namespace interpreter {

static const int kLastParamRegisterIndex =
    (InterpreterFrameConstants::kRegisterFileFromFp -
     InterpreterFrameConstants::kLastParamFromFp) /
    kPointerSize;
static const int kFunctionClosureRegisterIndex =
    (InterpreterFrameConstants::kRegisterFileFromFp -
     StandardFrameConstants::kFunctionOffset) /
    kPointerSize;
static const int kCurrentContextRegisterIndex =
    (InterpreterFrameConstants::kRegisterFileFromFp -
     StandardFrameConstants::kContextOffset) /
    kPointerSize;
static const int kBytecodeArrayRegisterIndex =
    (InterpreterFrameConstants::kRegisterFileFromFp -
     InterpreterFrameConstants::kBytecodeArrayFromFp) /
    kPointerSize;
static const int kBytecodeOffsetRegisterIndex =
    (InterpreterFrameConstants::kRegisterFileFromFp -
     InterpreterFrameConstants::kBytecodeOffsetFromFp) /
    kPointerSize;
static const int kCallerPCOffsetRegisterIndex =
    (InterpreterFrameConstants::kRegisterFileFromFp -
     InterpreterFrameConstants::kCallerPCOffsetFromFp) /
    kPointerSize;

AsmRegister AsmRegister::FromParameterIndex(int index, int parameter_count) {
  DCHECK_GE(index, 0);
  DCHECK_LT(index, parameter_count);
  int register_index = kLastParamRegisterIndex - parameter_count + index + 1;
  DCHECK_LT(register_index, 0);
  return AsmRegister(register_index);
}

int AsmRegister::ToParameterIndex(int parameter_count) const {
  DCHECK(is_parameter());
  return index() - kLastParamRegisterIndex + parameter_count - 1;
}

AsmRegister AsmRegister::function_closure() {
  return AsmRegister(kFunctionClosureRegisterIndex);
}

bool AsmRegister::is_function_closure() const {
  return index() == kFunctionClosureRegisterIndex;
}

AsmRegister AsmRegister::current_context() {
  return AsmRegister(kCurrentContextRegisterIndex);
}

bool AsmRegister::is_current_context() const {
  return index() == kCurrentContextRegisterIndex;
}

AsmRegister AsmRegister::bytecode_array() {
  return AsmRegister(kBytecodeArrayRegisterIndex);
}

bool AsmRegister::is_bytecode_array() const {
  return index() == kBytecodeArrayRegisterIndex;
}

AsmRegister AsmRegister::bytecode_offset() {
  return AsmRegister(kBytecodeOffsetRegisterIndex);
}

bool AsmRegister::is_bytecode_offset() const {
  return index() == kBytecodeOffsetRegisterIndex;
}

// static
AsmRegister AsmRegister::virtual_accumulator() {
  return AsmRegister(kCallerPCOffsetRegisterIndex);
}

OperandSize AsmRegister::SizeOfOperand() const {
  int32_t operand = ToOperand();
  if (operand >= kMinInt8 && operand <= kMaxInt8) {
    return OperandSize::kByte;
  } else if (operand >= kMinInt16 && operand <= kMaxInt16) {
    return OperandSize::kShort;
  } else {
    return OperandSize::kQuad;
  }
}

bool AsmRegister::AreContiguous(AsmRegister reg1, AsmRegister reg2,
                                AsmRegister reg3, AsmRegister reg4,
                                AsmRegister reg5) {
  if (reg1.index() + 1 != reg2.index()) {
    return false;
  }
  if (reg3.is_valid() && reg2.index() + 1 != reg3.index()) {
    return false;
  }
  if (reg4.is_valid() && reg3.index() + 1 != reg4.index()) {
    return false;
  }
  if (reg5.is_valid() && reg4.index() + 1 != reg5.index()) {
    return false;
  }
  return true;
}

std::string AsmRegister::ToString(int parameter_count) const {
  if (is_current_context()) {
    return std::string("<context>");
  } else if (is_function_closure()) {
    return std::string("<closure>");
  } else if (is_parameter()) {
    int parameter_index = ToParameterIndex(parameter_count);
    if (parameter_index == 0) {
      return std::string("<this>");
    } else {
      std::ostringstream s;
      s << "a" << parameter_index - 1;
      return s.str();
    }
  } else {
    std::ostringstream s;
    s << "r" << index();
    return s.str();
  }
}

}  // namespace interpreter
}  // namespace internal
}  // namespace v8
