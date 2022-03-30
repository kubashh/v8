// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/turboshaft/operations.h"

#include <atomic>
#include <sstream>

#include "src/base/platform/platform.h"
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

int CountDecimalDigits(uint32_t value) {
  int result = 1;
  while (value > 9) {
    result++;
    value = value / 10;
  }
  return result;
}

std::ostream& operator<<(std::ostream& os, Padding padding) {
  if (padding.spaces > 10000) return os;
  for (int i = 0; i < padding.spaces; ++i) {
    os << ' ';
  }
  return os;
}

std::ostream& operator<<(std::ostream& os, OperationPrintStyle styled_op) {
  const Operation& op = styled_op.op;
  os << OpcodeName(op.opcode) << "(";
  bool first = true;
  for (OpIndex input : op.inputs()) {
    if (!first) os << ", ";
    first = false;
    os << styled_op.op_index_prefix << input.id();
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

void Print(const Operation& op) { std::cout << op << "\n"; }

std::string ToString(const Operation& op) {
  std::stringstream stream;
  stream << op;
  return stream.str();
}

void PendingLoopPhiOp::PrintOptions(std::ostream& os) const {
  os << "[" << rep << ", #o" << old_backedge_index.id() << "]";
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

void ParameterOp::PrintOptions(std::ostream& os) const {
  os << "[" << parameter_index;
  if (debug_name) os << ", " << debug_name;
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
      os << "signed mul, ";
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
  os << rep;
  os << "]";
}

void OverflowCheckedBinaryOp::PrintOptions(std::ostream& os) const {
  os << "[";
  switch (kind) {
    case Kind::kSignedAdd:
      os << "signed add, ";
      break;
    case Kind::kSignedSub:
      os << "signed sub, ";
      break;
    case Kind::kSignedMul:
      os << "signed mul, ";
      break;
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

#ifdef DEBUG
bool ShouldSkipOptimizationStep() {
  static std::atomic<uint64_t> counter{0};
  uint64_t current = counter++;
  if (current == FLAG_turboshaft_break_on_optimization_step) {
    base::OS::DebugBreak();
  }
  if (current >= FLAG_turboshaft_limit_optimization_steps) {
    return true;
  }
  return false;
}
#endif  // DEBUG

}  // namespace turboshaft
}  // namespace compiler
}  // namespace internal
}  // namespace v8
