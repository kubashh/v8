// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/turboshaft/operations.h"

#include <sstream>

#include "src/common/assert-scope.h"
#include "src/compiler/frame-states.h"
#include "src/compiler/turboshaft/graph.h"
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
    case Kind::kNumber:
      os << "number: " << number();
      break;
    case Kind::kTaggedIndex:
      os << "tagged index: " << tagged_index();
      break;
    case Kind::kFloat64:
      os << "float64: " << float64();
      break;
    case Kind::kFloat32:
      os << "float32: " << float32();
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
    case Kind::kDelayedString:
      os << delayed_string();
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
  if (offset != 0) os << ", offset: " << offset;
  os << "]";
}

void IndexedLoadOp::PrintOptions(std::ostream& os) const {
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
  if (element_scale != 0) os << ", element scale: 2^" << int{element_scale};
  if (offset != 0) os << ", offset: " << offset;
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
  if (offset != 0) os << ", offset: " << offset;
  os << "]";
}

void IndexedStoreOp::PrintOptions(std::ostream& os) const {
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
  if (element_scale != 0) os << ", element scale: 2^" << int{element_scale};
  if (offset != 0) os << ", offset: " << offset;
  os << "]";
}

void FrameStateOp::PrintOptions(std::ostream& os) const {
  os << "[";
  os << (inlined ? "inlined" : "not inlined");
  os << ", ";
  os << data->frame_state_info;
  os << ", state values:";
  FrameStateData::Iterator it = data->iterator(state_values());
  while (it.has_more()) {
    os << " ";
    switch (it.current_instr()) {
      case FrameStateData::Instr::kInput: {
        MachineType type;
        OpIndex input;
        it.ConsumeInput(&type, &input);
        os << "#" << input.id() << "(" << type << ")";
        break;
      }
      case FrameStateData::Instr::kUnusedRegister:
        it.ConsumeUnusedRegister();
        os << ".";
        break;
      case FrameStateData::Instr::kDematerializedObject: {
        uint32_t id;
        uint32_t field_count;
        it.ConsumeDematerializedObject(&id, &field_count);
        os << "$" << id << "(field count: " << field_count << ")";
        break;
      }
      case FrameStateData::Instr::kDematerializedObjectReference: {
        uint32_t id;
        it.ConsumeDematerializedObjectReference(&id);
        os << "$" << id;
        break;
      }
    }
  }
  os << "]";
}

void BinaryOp::PrintOptions(std::ostream& os) const {
  os << "[";
  switch (kind) {
    case Kind::kAdd:
      os << "add, ";
      break;
    case Kind::kSub:
      os << "sub, ";
      break;
    case Kind::kMul:
      os << "mul, ";
      break;
    case Kind::kBitwiseAnd:
      os << "bitwise and, ";
      break;
    case Kind::kBitwiseOr:
      os << "bitwise or, ";
      break;
    case Kind::kBitwiseXor:
      os << "bitwise xor, ";
      break;
  }
  if (with_overflow_bit) {
    os << "with overflow bit, ";
  }
  os << rep;
  os << "]";
}

std::ostream& operator<<(std::ostream& os, BlockIndex b) {
  if (b == BlockIndex::kInvalid) {
    return os << "<invalid block>";
  }
  return os << "B" << ToUnderlyingType(b);
}

std::ostream& operator<<(std::ostream& os, const Block* b) {
  return os << b->index;
}

void SwitchOp::PrintOptions(std::ostream& os) const {
  os << "[";
  for (const Case& c : cases) {
    os << "case " << c.value << ": " << c.destination << ", ";
  }
  os << " default: " << default_case << "]";
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
