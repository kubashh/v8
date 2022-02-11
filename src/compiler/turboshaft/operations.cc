// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/turboshaft/operations.h"

#include <sstream>

#include "src/handles/handles-inl.h"

namespace v8 {
namespace internal {
namespace compiler {
namespace turboshaft {

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
  for (OpIndex input : op.inputs()) {
    if (!first) os << ", ";
    first = false;
    os << "#" << input.id();
  }
  os << ")";
  switch (op.opcode) {
#define SWITCH_CASE(Name)                 \
  case Opcode::k##Name:                   \
    op.Cast<Name##Op>().PrintOptions(os); \
    break;
    TURBOSHAFT_OPERATION_LIST(SWITCH_CASE)
#undef SWITCH_CASE
  }
  return os;
}

void ConstantOp::PrintOptions(std::ostream& os) const {
  os << "[";
  switch (kind) {
    case Kind::kWord32:
      os << "word32: " << static_cast<int32_t>(storage.integral);
      break;
    case Kind::kWord64:
      os << "word64: " << static_cast<int64_t>(storage.integral);
      break;
    case Kind::kExternal:
      os << "external: " << external_reference();
      break;
    case Kind::kHeapObject:
      os << "heap object: " << handle();
      break;
    case Kind::kCompressedHeapObject:
      os << "compressed heap object: " << handle();
      break;
  }
  os << "]";
}

void LoadOp::PrintOptions(std::ostream& os) const {
  os << "[";
  switch (kind) {
    case Kind::kRaw:
      os << "raw";
      break;
    case Kind::kOnHeap:
      os << "on heap";
      break;
  }
  os << ", " << loaded_rep;
  os << ", offset: " << offset;
  os << "]";
}

void StoreOp::PrintOptions(std::ostream& os) const {
  os << "[";
  switch (kind) {
    case Kind::kRaw:
      os << "raw";
      break;
    case Kind::kOnHeap:
      os << "on heap";
      break;
  }
  os << ", " << stored_rep;
  os << ", " << write_barrier;
  os << ", offset: " << offset;
  os << "]";
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
