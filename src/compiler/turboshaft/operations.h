// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_OPERATIONS_H_
#define V8_COMPILER_TURBOSHAFT_OPERATIONS_H_

#include <cstring>
#include <limits>
#include <type_traits>

#include "src/base/macros.h"
#include "src/base/vector.h"
#include "src/codegen/external-reference.h"
#include "src/codegen/machine-type.h"
#include "src/common/globals.h"
#include "src/compiler/globals.h"
#include "src/zone/zone.h"

namespace v8 {
namespace internal {

class HeapObject;

namespace compiler {

class CallDescriptor;
class Node;
class FrameStateInfo;

namespace turboshaft {

struct Block;
class Variable;
class Graph;

template <class T>
constexpr std::underlying_type_t<T> ToUnderlyingType(T x) {
  STATIC_ASSERT(std::is_enum<T>::value);
  return static_cast<std::underlying_type_t<T>>(x);
}

#define TURBOSHAFT_OPERATION_LIST(V) \
  V(Add)                             \
  V(Sub)                             \
  V(BitwiseAnd)                      \
  V(Equal)                           \
  V(PendingVariableLoopPhi)          \
  V(PendingLoopPhi)                  \
  V(Constant)                        \
  V(Load)                            \
  V(Parameter)                       \
  V(Goto)                            \
  V(StackPointerGreaterThan)         \
  V(LoadStackCheckOffset)            \
  V(CheckLazyDeopt)                  \
  V(Phi)                             \
  V(Checkpoint)                      \
  V(Call)                            \
  V(Return)                          \
  V(Branch)

enum class Opcode : uint16_t {
#define ENUM_CONSTANT(Name) k##Name,
  TURBOSHAFT_OPERATION_LIST(ENUM_CONSTANT)
#undef ENUM_CONSTANT
};

const char* OpcodeName(Opcode opcode);

#define COUNT_OPCODES(Name) +1
constexpr uint16_t kNumberOfOpcodes =
    0 TURBOSHAFT_OPERATION_LIST(COUNT_OPCODES);
#undef COUNT_OPCODES

// Operations are stored in possibly muliple sequential storage slots.
using OperationStorageSlot = std::aligned_storage_t<8, 8>;
// Operations occupy at least 2 slots, therefore we assign one id per two slots.
constexpr size_t kSlotsPerId = 2;
class OpIndex {
 public:
  explicit constexpr OpIndex(uint32_t offset) : offset_(offset) {
    DCHECK_EQ(offset % sizeof(OperationStorageSlot), 0);
  }
  constexpr OpIndex() : offset_(std::numeric_limits<uint32_t>::max()) {}

  uint32_t id() const {
    return offset_ / sizeof(OperationStorageSlot) / kSlotsPerId;
  }
  uint32_t offset() const { return offset_; }

  bool valid() const { return *this != Invalid(); }

  static constexpr OpIndex Invalid() { return OpIndex(); }

  bool operator==(OpIndex other) const { return offset_ == other.offset_; }
  bool operator!=(OpIndex other) const { return offset_ != other.offset_; }
  bool operator<(OpIndex other) const { return offset_ < other.offset_; }
  bool operator>(OpIndex other) const { return offset_ > other.offset_; }
  bool operator<=(OpIndex other) const { return offset_ <= other.offset_; }
  bool operator>=(OpIndex other) const { return offset_ >= other.offset_; }

 private:
  uint32_t offset_;
};

// Use enum class to prevent implicit conversions.
enum class BlockIndex : uint32_t {
  kInvalid = std::numeric_limits<uint32_t>::max()
};

struct OpProperties {
  const bool can_read;
  const bool can_write;
  const bool non_memory_side_effects;
  const bool is_block_terminator;
  // By being const and not being set in the constructor, these properties are
  // guaranteed to be derived.
  const bool is_pure = !(can_read || can_write || non_memory_side_effects ||
                         is_block_terminator);
  const bool is_required_when_unused =
      can_write || non_memory_side_effects || is_block_terminator;

  constexpr OpProperties(bool can_read, bool can_write,
                         bool non_memory_side_effects, bool is_block_terminator)
      : can_read(can_read),
        can_write(can_write),
        non_memory_side_effects(non_memory_side_effects),
        is_block_terminator(is_block_terminator) {}

  static constexpr OpProperties Pure() { return {false, false, false, false}; }
  static constexpr OpProperties Reading() {
    return {true, false, false, false};
  }
  static constexpr OpProperties Writing() {
    return {false, true, false, false};
  }
  static constexpr OpProperties NonMemorySideEffects() {
    return {false, false, true, false};
  }
  static constexpr OpProperties AnySideEffects() {
    return {true, true, true, false};
  }
  static constexpr OpProperties BlockTerminator() {
    return {false, false, false, true};
  }
};

struct alignas(OpIndex) Operation {
  const Opcode opcode;
  uint16_t input_count;

  base::Vector<OpIndex> inputs();
  base::Vector<const OpIndex> inputs() const;

  template <class Op>
  struct opcode_of {};

  static size_t StorageSlotCount(Opcode opcode, size_t input_count);
  size_t StorageSlotCount() const {
    return StorageSlotCount(opcode, input_count);
  }

  template <class Op>
  bool Is() const {
    return opcode == Op::opcode();
  }
  template <class Op>
  Op& Cast() {
    DCHECK(Is<Op>());
    return *static_cast<Op*>(this);
  }
  template <class Op>
  const Op& Cast() const {
    DCHECK(Is<Op>());
    return *static_cast<const Op*>(this);
  }
  template <class Op>
  const Op* TryCast() const {
    if (!Is<Op>()) return nullptr;
    return static_cast<const Op*>(this);
  }
  template <class Op>
  Op* TryCast() {
    if (!Is<Op>()) return nullptr;
    return static_cast<Op*>(this);
  }
  size_t InputCount() const;
  OpProperties properties() const;

  std::string ToString() const;

  // Operation objects store their inputs behind the object. Therefore, they can
  // only be constructed as part of a Graph.
 protected:
  explicit Operation(Opcode opcode) : opcode(opcode) {}

  Operation(const Operation&) = delete;
  Operation& operator=(const Operation&) = delete;
};

OperationStorageSlot* AllocateOpStorage(Graph* graph, size_t slot_count);

template <class Derived>
struct OperationT : Operation {
  // Enable concise base-constructor call in derived struct.
  using Base = OperationT;

  static constexpr bool CanRead() { return Derived::properties.can_read; }
  static constexpr bool CanWrite() { return Derived::properties.can_write; }
  static constexpr bool IsBlockTerminator() {
    return Derived::properties.is_block_terminator;
  }
  static constexpr bool IsRequiredWhenUnused() {
    return Derived::properties.is_required_when_unused;
  }
  // This purposefully overshadows Operation::opcode, because it's better to
  // use the static value when we already know the Operation type.
  static constexpr Opcode opcode() {
    return Operation::opcode_of<Derived>::value;
  }

  static size_t StorageSlotCount(size_t input_count) {
    constexpr size_t r = sizeof(OperationStorageSlot) / sizeof(OpIndex);
    STATIC_ASSERT(sizeof(OperationStorageSlot) % sizeof(OpIndex) == 0);
    STATIC_ASSERT(sizeof(Derived) % sizeof(OpIndex) == 0);
    size_t result = std::max<size_t>(
        2, (r - 1 + sizeof(Derived) / sizeof(OpIndex) + input_count) / r);
    DCHECK_EQ(result,
              Operation::StorageSlotCount(Derived::opcode(), input_count));
    return result;
  }
  size_t StorageSlotCount() const { return StorageSlotCount(input_count); }

  static Derived& New(Graph* graph, size_t input_count) {
    OperationStorageSlot* ptr =
        AllocateOpStorage(graph, StorageSlotCount(input_count));
    Derived* result = new (ptr) Derived();
    DCHECK_LE(input_count, std::numeric_limits<uint16_t>::max());
    result->Operation::input_count = static_cast<uint16_t>(input_count);
    return *result;
  }
  static Derived& New(Graph* graph, base::Vector<const OpIndex> inputs) {
    Derived& result = New(graph, inputs.size());
    result.inputs().OverwriteWith(inputs);
    return result;
  }

  OperationT() : Operation(opcode()) {
    STATIC_ASSERT((std::is_base_of<OperationT, Derived>::value));
    STATIC_ASSERT(std::is_trivially_copyable<Derived>::value);
    STATIC_ASSERT(std::is_trivially_destructible<Derived>::value);
  }
};

template <size_t InputCount, class Derived>
struct FixedOperationT : OperationT<Derived> {
  // Enable concise base access in derived struct.
  using Base = FixedOperationT;

  static constexpr uint16_t input_count = InputCount;
  base::Vector<OpIndex> inputs() {
    return {reinterpret_cast<OpIndex*>(reinterpret_cast<char*>(this) +
                                       sizeof(Derived)),
            InputCount};
  }
  base::Vector<const OpIndex> inputs() const {
    return {reinterpret_cast<const OpIndex*>(
                reinterpret_cast<const char*>(this) + sizeof(Derived)),
            InputCount};
  }

  // Redefine the input initialization to tell C++ about the static input size.
  static Derived& New(Graph* graph) {
    STATIC_ASSERT(InputCount == 0);
    return OperationT<Derived>::New(graph, InputCount);
  }
  static Derived& New(Graph* graph,
                      const std::array<OpIndex, InputCount>& inputs) {
    Derived& result = OperationT<Derived>::New(graph, InputCount);
    result.inputs().OverwriteWith(
        base::Vector<const OpIndex>{inputs.data(), InputCount});
    return result;
  }
};

struct AddOp : FixedOperationT<2, AddOp> {
  MachineRepresentation rep;

  static constexpr OpProperties properties = OpProperties::Pure();

  OpIndex left() const { return inputs()[0]; }
  OpIndex right() const { return inputs()[1]; }

  static AddOp& New(Graph* graph, OpIndex left, OpIndex right,
                    MachineRepresentation rep) {
    AddOp& op = Base::New(graph, {left, right});
    op.rep = rep;
    return op;
  }
};

struct SubOp : FixedOperationT<2, SubOp> {
  MachineRepresentation rep;

  static constexpr OpProperties properties = OpProperties::Pure();

  OpIndex left() const { return inputs()[0]; }
  OpIndex right() const { return inputs()[1]; }

  static SubOp& New(Graph* graph, OpIndex left, OpIndex right,
                    MachineRepresentation rep) {
    SubOp& op = Base::New(graph, {left, right});
    op.rep = rep;
    return op;
  }
};

struct BitwiseAndOp : FixedOperationT<2, BitwiseAndOp> {
  MachineRepresentation rep;

  static constexpr OpProperties properties = OpProperties::Pure();

  OpIndex left() const { return inputs()[0]; }
  OpIndex right() const { return inputs()[1]; }

  static BitwiseAndOp& New(Graph* graph, OpIndex left, OpIndex right,
                           MachineRepresentation rep) {
    BitwiseAndOp& op = Base::New(graph, {left, right});
    op.rep = rep;
    return op;
  }
};

struct EqualOp : FixedOperationT<2, EqualOp> {
  MachineRepresentation rep;

  static constexpr OpProperties properties = OpProperties::Pure();

  OpIndex left() const { return inputs()[0]; }
  OpIndex right() const { return inputs()[1]; }

  static EqualOp& New(Graph* graph, OpIndex left, OpIndex right,
                      MachineRepresentation rep) {
    EqualOp& op = Base::New(graph, {left, right});
    op.rep = rep;
    return op;
  }
};

struct PhiOp : OperationT<PhiOp> {
  static constexpr OpProperties properties = OpProperties::Pure();

  static PhiOp& New(Graph* graph, base::Vector<const OpIndex> inputs) {
    PhiOp& op = Base::New(graph, inputs);
    return op;
  }
};

// Only used while VarAssembler is running and the loop backedge has not been
// emitted yet.
struct PendingVariableLoopPhiOp : FixedOperationT<1, PendingVariableLoopPhiOp> {
  Variable* variable;

  static constexpr OpProperties properties = OpProperties::Pure();

  OpIndex first() const { return inputs()[0]; }

  static PendingVariableLoopPhiOp& New(Graph* graph, OpIndex first,
                                       Variable* variable) {
    PendingVariableLoopPhiOp& op = Base::New(graph, {first});
    op.variable = variable;
    return op;
  }
};

// Only used when moving a loop phi to a new graph while the loop backedge has
// not been emitted yet.
struct PendingLoopPhiOp : FixedOperationT<1, PendingLoopPhiOp> {
  union {
    // Used when transforming a TurboShaft graph.
    // This is not an input because it refers to the old graph.
    OpIndex old_backedge_index = OpIndex::Invalid();
    // Used when translating from sea-of-nodes.
    Node* old_backedge_node;
  };

  static constexpr OpProperties properties = OpProperties::Pure();

  OpIndex first() const { return inputs()[0]; }

  static PendingLoopPhiOp& New(Graph* graph, OpIndex first,
                               OpIndex old_backedge_index) {
    DCHECK(old_backedge_index.valid());
    PendingLoopPhiOp& op = Base::New(graph, {first});
    op.old_backedge_index = old_backedge_index;
    return op;
  }
  static PendingLoopPhiOp& New(Graph* graph, OpIndex first,
                               Node* old_backedge_node) {
    PendingLoopPhiOp& op = Base::New(graph, {first});
    op.old_backedge_node = old_backedge_node;
    return op;
  }
};

struct ConstantOp : FixedOperationT<0, ConstantOp> {
  enum class Kind : uint8_t {
    kWord32,
    kWord64,
    kExternal,
    kHeapObject,
    kCompressedHeapObject
  };

  Kind kind;
  union Storage {
    uint64_t integral;
    ExternalReference external;
    Handle<HeapObject> handle;
    Storage(uint64_t integral = 0) : integral(integral) {}
    Storage(ExternalReference constant) : external(constant) {}
    Storage(Handle<HeapObject> constant) : handle(constant) {}
  } storage;

  static constexpr OpProperties properties = OpProperties::Pure();

  MachineRepresentation Representation() const {
    switch (kind) {
      case Kind::kWord32:
        return MachineRepresentation::kWord32;
      case Kind::kWord64:
        return MachineRepresentation::kWord64;
      case Kind::kExternal:
        return MachineType::PointerRepresentation();
      case Kind::kHeapObject:
        return MachineRepresentation::kTagged;
      case Kind::kCompressedHeapObject:
        return MachineRepresentation::kCompressed;
    }
  }

  static ConstantOp& NewWord32(Graph* graph, uint32_t constant) {
    ConstantOp& op = Base::New(graph, {});
    op.kind = Kind::kWord32;
    op.storage.integral = constant;
    return op;
  }
  static ConstantOp& NewWord64(Graph* graph, uint64_t constant) {
    ConstantOp& op = Base::New(graph, {});
    op.kind = Kind::kWord64;
    op.storage.integral = constant;
    return op;
  }
  static ConstantOp& NewExternal(Graph* graph, ExternalReference constant) {
    ConstantOp& op = Base::New(graph, {});
    op.kind = Kind::kExternal;
    op.storage.external = constant;
    return op;
  }
  static ConstantOp& NewHeapObject(Graph* graph,
                                   Handle<i::HeapObject> constant) {
    ConstantOp& op = Base::New(graph, {});
    op.kind = Kind::kHeapObject;
    op.storage.handle = constant;
    return op;
  }
  static ConstantOp& NewCompressedHeapObject(Graph* graph,
                                             Handle<i::HeapObject> constant) {
    ConstantOp& op = Base::New(graph, {});
    op.kind = Kind::kCompressedHeapObject;
    op.storage.handle = constant;
    return op;
  }

  static ConstantOp& New(Graph* graph, Kind kind, Storage storage) {
    ConstantOp& op = Base::New(graph, {});
    op.kind = kind;
    op.storage = storage;
    return op;
  }

  uint32_t word32() const {
    DCHECK_EQ(kind, Kind::kWord32);
    return static_cast<uint32_t>(storage.integral);
  }

  uint64_t word64() const {
    DCHECK_EQ(kind, Kind::kWord64);
    return static_cast<uint64_t>(storage.integral);
  }

  ExternalReference external_reference() const {
    DCHECK_EQ(kind, Kind::kExternal);
    return storage.external;
  }

  Handle<i::HeapObject> handle() const {
    DCHECK(kind == Kind::kHeapObject || kind == Kind::kCompressedHeapObject);
    return storage.handle;
  }
};

struct LoadOp : FixedOperationT<1, LoadOp> {
  enum class Kind : uint8_t { kOnHeap, kRaw };
  MachineType loaded_rep;
  Kind kind;
  int32_t offset;

  static constexpr OpProperties properties = OpProperties::Reading();

  OpIndex base() const { return inputs()[0]; }

  static LoadOp& New(Graph* graph, Kind kind, MachineType loaded_rep,
                     OpIndex base, int32_t offset = 0) {
    LoadOp& op = Base::New(graph, {base});
    op.kind = kind;
    op.loaded_rep = loaded_rep;
    op.offset = offset;
    return op;
  }
};

struct StackPointerGreaterThanOp
    : FixedOperationT<1, StackPointerGreaterThanOp> {
  StackCheckKind kind;

  static constexpr OpProperties properties = OpProperties::Reading();

  OpIndex stack_limit() const { return inputs()[0]; }

  static StackPointerGreaterThanOp& New(Graph* graph, StackCheckKind kind,
                                        OpIndex stack_limit) {
    StackPointerGreaterThanOp& op = Base::New(graph, {stack_limit});
    op.kind = kind;
    return op;
  }
};

struct LoadStackCheckOffsetOp : FixedOperationT<0, LoadStackCheckOffsetOp> {
  static constexpr OpProperties properties = OpProperties::Pure();
  using Base::New;
};

// A Checkpoint Operation describes a potential deopt point.
// Multiple check operations can go back to the same checkpoint.
// Checkpoints can depend on a preceding and dominating checkpoint to enable a
// more compact encoding of deopt information.
struct CheckpointOp : OperationT<CheckpointOp> {
  enum class Kind {
    kFull,          // A self-contained, non-inlined checkpoint.
    kDifferential,  // Modifies a dominating checkpoint to save space.
    kInlined        // Extends a dominating checkpoint by adding a new frame.
  };

  // Kind kind;
  // const FrameStateInfo& frame_state_info;
  // TOIMPLEMENT: StateValue information.

  // TODO(tebbi): Is it pure?
  static constexpr OpProperties properties = OpProperties::Reading();

  // static CheckpointOp Full(base::Vector<const OpIndex> inputs,
  //                          const FrameStateInfo& frame_state_info, Zone*
  //                          zone) {
  //   CheckpointOp result =
  //       CheckpointOp(inputs.size(), Data{Kind::kFull, frame_state_info},
  //       zone);
  //   result.Inputs().OverwriteWith(inputs);
  //   return result;
  // }

  // static CheckpointOp Differential(OpIndex base_checkpoint,
  //                                  base::Vector<const OpIndex> inputs,
  //                                  const FrameStateInfo& frame_state_info,
  //                                  Zone* zone) {
  //   CheckpointOp result = CheckpointOp(
  //       inputs.size() + 1, Data{Kind::kDifferential, frame_state_info},
  //       zone);
  //   base::Vector<OpIndex> v = result.Inputs();
  //   v[0] = base_checkpoint;
  //   v.SubVector(1, v.size()).OverwriteWith(inputs);
  //   return result;
  // }

  static CheckpointOp& New(Graph* graph, base::Vector<const OpIndex> inputs) {
    return Base::New(graph, inputs);
  }
};

// CheckLazyDeoptOp should always immediately follow a call and a checkpoint.
// Semantically, it deopts to the checkpoint if the current code object has been
// deoptimized. But this might also be implemented differently.
struct CheckLazyDeoptOp : FixedOperationT<1, CheckLazyDeoptOp> {
  static constexpr OpProperties properties =
      OpProperties::NonMemorySideEffects();

  OpIndex checkpoint() const { return inputs()[0]; }

  static CheckLazyDeoptOp& New(Graph* graph, OpIndex checkpoint) {
    return Base::New(graph, {checkpoint});
  }
};

struct ParameterOp : FixedOperationT<0, ParameterOp> {
  uint32_t parameter_index;
  const char* debug_name;

  static constexpr OpProperties properties = OpProperties::Pure();

  static ParameterOp& New(Graph* graph, uint32_t parameter_index,
                          const char* debug_name = "") {
    ParameterOp& op = Base::New(graph);
    op.parameter_index = parameter_index;
    op.debug_name = debug_name;
    return op;
  }
};

struct CallOp : OperationT<CallOp> {
  const CallDescriptor* descriptor;

  static constexpr OpProperties properties = OpProperties::AnySideEffects();

  OpIndex callee() const { return inputs()[0]; }
  base::Vector<const OpIndex> arguments() const {
    return inputs().SubVector(1, input_count);
  }

  static CallOp& New(Graph* graph, const CallDescriptor* descriptor,
                     OpIndex callee, base::Vector<const OpIndex> arguments) {
    CallOp& op = Base::New(graph, 1 + arguments.size());
    op.descriptor = descriptor;
    base::Vector<OpIndex> inputs = op.inputs();
    inputs[0] = callee;
    inputs.SubVector(1, inputs.size()).OverwriteWith(arguments);
    return op;
  }
};

struct ReturnOp : OperationT<ReturnOp> {
  int32_t pop_count;

  static constexpr OpProperties properties = OpProperties::BlockTerminator();

  base::Vector<const OpIndex> return_values() const { return inputs(); }

  static ReturnOp& New(Graph* graph, base::Vector<const OpIndex> return_values,
                       int32_t pop_count) {
    ReturnOp& op = Base::New(graph, return_values);
    op.pop_count = pop_count;
    return op;
  }
};

struct GotoOp : FixedOperationT<0, GotoOp> {
  Block* destination;

  static constexpr OpProperties properties = OpProperties::BlockTerminator();

  static GotoOp& New(Graph* graph, Block* destination) {
    GotoOp& op = Base::New(graph);
    op.destination = destination;
    return op;
  }
};

struct BranchOp : FixedOperationT<1, BranchOp> {
  Block* if_true;
  Block* if_false;

  static constexpr OpProperties properties = OpProperties::BlockTerminator();

  OpIndex condition() const { return inputs()[0]; }

  static BranchOp& New(Graph* graph, OpIndex condition, Block* if_true,
                       Block* if_false) {
    BranchOp& op = Base::New(graph, {condition});
    op.if_true = if_true;
    op.if_false = if_false;
    return op;
  }
};

#define OPERATION_PROPERTIES_CASE(Name) Name##Op::properties,
static constexpr OpProperties kOperationPropertiesTable[kNumberOfOpcodes] = {
    TURBOSHAFT_OPERATION_LIST(OPERATION_PROPERTIES_CASE)};
#undef OPERATION_PROPERTIES_CASE

#define OPERATION_OPCODE_MAP_CASE(Name) \
  template <>                           \
  struct Operation::opcode_of<Name##Op> \
      : std::integral_constant<Opcode, Opcode::k##Name> {};
TURBOSHAFT_OPERATION_LIST(OPERATION_OPCODE_MAP_CASE)
#undef OPERATION_OPCODE_MAP_CASE

template <class Op, class = void>
struct static_operation_input_count : std::integral_constant<uint32_t, 0> {};
template <class Op>
struct static_operation_input_count<Op, base::void_t<decltype(Op::inputs)>>
    : std::integral_constant<uint32_t, sizeof(Op::inputs) / sizeof(OpIndex)> {};
constexpr size_t kOperationSizeTable[kNumberOfOpcodes] = {
#define OPERATION_SIZE(Name) sizeof(Name##Op),
    TURBOSHAFT_OPERATION_LIST(OPERATION_SIZE)
#undef OPERATION_SIZE
};
constexpr size_t kOperationSizeDividedBySizeofOpIndexTable[kNumberOfOpcodes] = {
#define OPERATION_SIZE(Name) (sizeof(Name##Op) / sizeof(OpIndex)),
    TURBOSHAFT_OPERATION_LIST(OPERATION_SIZE)
#undef OPERATION_SIZE
};

inline base::Vector<OpIndex> Operation::inputs() {
  OpIndex* ptr =
      reinterpret_cast<OpIndex*>(reinterpret_cast<char*>(this) +
                                 kOperationSizeTable[ToUnderlyingType(opcode)]);
  return {ptr, input_count};
}
inline base::Vector<const OpIndex> Operation::inputs() const {
  const OpIndex* ptr = reinterpret_cast<const OpIndex*>(
      reinterpret_cast<const char*>(this) +
      kOperationSizeTable[ToUnderlyingType(opcode)]);
  return {ptr, input_count};
}

inline OpProperties Operation::properties() const {
  return kOperationPropertiesTable[ToUnderlyingType(opcode)];
}

// static
inline size_t Operation::StorageSlotCount(Opcode opcode, size_t input_count) {
  size_t size =
      kOperationSizeDividedBySizeofOpIndexTable[ToUnderlyingType(opcode)];
  constexpr size_t r = sizeof(OperationStorageSlot) / sizeof(OpIndex);
  STATIC_ASSERT(sizeof(OperationStorageSlot) % sizeof(OpIndex) == 0);
  return std::max<size_t>(2, (r - 1 + size + input_count) / r);
}

}  // namespace turboshaft
}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_TURBOSHAFT_OPERATIONS_H_
