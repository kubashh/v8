// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_OPERATIONS_H_
#define V8_COMPILER_TURBOSHAFT_OPERATIONS_H_

#include <cstring>
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

template <class T>
constexpr std::underlying_type_t<T> ToUnderlyingType(T x) {
  STATIC_ASSERT(std::is_enum<T>::value);
  return static_cast<std::underlying_type_t<T>>(x);
}

#define TURBOSHAFT_INLINE_OPERATION_LIST(V) \
  V(Add)                                    \
  V(Sub)                                    \
  V(BitwiseAnd)                             \
  V(Equal)                                  \
  V(BinaryPhi)                              \
  V(LoopPhi)                                \
  V(PendingVariableLoopPhi)                 \
  V(PendingLoopPhi)                         \
  V(Constant)                               \
  V(Load)                                   \
  V(Parameter)                              \
  V(Return)                                 \
  V(Goto)                                   \
  V(StackPointerGreaterThan)                \
  V(LoadStackCheckOffset)                   \
  V(CheckLazyDeopt)

#define TURBOSHAFT_OUT_OF_LINE_OPERATION_LIST(V) \
  V(Phi)                                         \
  V(Checkpoint)                                  \
  V(Call)                                        \
  V(Branch)

#define TURBOSHAFT_OPERATION_LIST(V)  \
  TURBOSHAFT_INLINE_OPERATION_LIST(V) \
  TURBOSHAFT_OUT_OF_LINE_OPERATION_LIST(V)

enum class Opcode : uint16_t {
#define ENUM_CONSTANT(Name) k##Name,
  TURBOSHAFT_OPERATION_LIST(ENUM_CONSTANT)
#undef ENUM_CONSTANT
};

const char* OpcodeName(Opcode opcode);

#define COUNT_OPCODES(Name) +1
constexpr uint16_t kNumberOfInlineOpcodes =
    0 TURBOSHAFT_INLINE_OPERATION_LIST(COUNT_OPCODES);
constexpr uint16_t kNumberOfOpcodes =
    kNumberOfInlineOpcodes TURBOSHAFT_OUT_OF_LINE_OPERATION_LIST(COUNT_OPCODES);
#undef COUNT_OPCODES

constexpr bool IsInlineOpcode(Opcode opcode) {
  return ToUnderlyingType(opcode) < kNumberOfInlineOpcodes;
}

// Use enum class to prevent implicit conversions.

enum class OpIndex : uint32_t {
  kInvalid = std::numeric_limits<uint32_t>::max()
};

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

// All operations can be stored with 16 bytes.
static constexpr size_t kOperationSize = 16;
using OperationStorage = std::aligned_storage_t<kOperationSize, alignof(void*)>;
STATIC_ASSERT(sizeof(OperationStorage) == kOperationSize);

struct Operation {
  Opcode opcode;

  template <class Op>
  struct opcode_of {};

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

  base::Vector<const OpIndex> Inputs() const;
  base::Vector<OpIndex> Inputs();
  size_t InputCount() const;
  OperationStorage ReplaceInputs(base::Vector<const OpIndex> inputs,
                                 Zone* zone) const;
  OpProperties properties() const;

  std::string ToString() const;

  explicit Operation(Opcode opcode) : opcode(opcode) {}
  static constexpr size_t kInputsOffset = 4;
};

template <class Derived, class Base = Operation>
struct OperationT : Base {
  template <class... Args>
  explicit OperationT(Args... args) : Base(opcode(), args...) {
    STATIC_ASSERT((std::is_base_of<OperationT, Derived>::value));
    STATIC_ASSERT(alignof(Derived) <= alignof(OperationStorage));
    STATIC_ASSERT(sizeof(Derived) <= sizeof(OperationStorage));
    STATIC_ASSERT(std::is_trivially_copyable<Derived>::value);
    STATIC_ASSERT(std::is_trivially_destructible<Derived>::value);
  }
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

  size_t InputCount() const;
};

struct OutOfLineOp : Operation {
  struct Storage {};
  uint32_t input_count;
  Storage* storage;

  base::Vector<const OpIndex> Inputs() const {
    return {reinterpret_cast<const OpIndex*>(
                reinterpret_cast<const char*>(storage) -
                input_count * sizeof(OpIndex)),
            input_count};
  }
  base::Vector<OpIndex> Inputs() {
    return {reinterpret_cast<OpIndex*>(reinterpret_cast<char*>(storage) -
                                       input_count * sizeof(OpIndex)),
            input_count};
  }
  size_t InputCount() const { return input_count; }

  explicit OutOfLineOp(Opcode opcode, uint32_t input_count)
      : Operation(opcode), input_count(input_count) {}
};

template <class Derived>
struct OutOfLineOpT : OperationT<Derived, OutOfLineOp> {
  struct Storage : OutOfLineOp::Storage {
    using Data = typename Derived::Data;
    Data data;

    explicit Storage(const Data& data) : data(data) {
      STATIC_ASSERT(std::is_trivially_destructible<Storage>::value);
      STATIC_ASSERT(std::is_trivially_copyable<Storage>::value);
      // Necessary to generically copy Storage in Operation::ReplaceInputs().
      STATIC_ASSERT(alignof(Storage) <= alignof(void*));
    }
  };
  const auto& data() const {
    return static_cast<Storage*>(this->storage)->data;
  }

  template <class Data>
  OutOfLineOpT(size_t input_count, const Data& data, Zone* zone)
      : OperationT<Derived, OutOfLineOp>(static_cast<uint32_t>(input_count)) {
    size_t inputs_size = input_count;
    if (alignof(Storage) > alignof(OpIndex)) {
      inputs_size = RoundUp(input_count * sizeof(OpIndex), alignof(Storage));
    }
    void* ptr = static_cast<char*>(
                    zone->Allocate<Storage>(inputs_size + sizeof(Storage))) +
                inputs_size;
    this->storage = new (ptr) Storage{data};
  }

  template <class Data>
  OutOfLineOpT(base::Vector<const OpIndex> inputs, const Data& data, Zone* zone)
      : OutOfLineOpT(inputs.size(), data, zone) {
    this->Inputs().OverwriteWith(inputs);
  }

  OutOfLineOpT(base::Vector<const OpIndex> inputs, Zone* zone)
      : OutOfLineOpT(inputs, typename Derived::Data{}, zone) {}
};

struct AddOp : OperationT<AddOp> {
  MachineRepresentation rep;
  OpIndex inputs[2];

  static constexpr OpProperties properties = OpProperties::Pure();

  OpIndex left() const { return inputs[0]; }
  OpIndex right() const { return inputs[1]; }

  AddOp(OpIndex left, OpIndex right, MachineRepresentation rep)
      : rep(rep), inputs{left, right} {}
};

struct SubOp : OperationT<SubOp> {
  MachineRepresentation rep;
  OpIndex inputs[2];

  static constexpr OpProperties properties = OpProperties::Pure();

  OpIndex left() const { return inputs[0]; }
  OpIndex right() const { return inputs[1]; }

  SubOp(OpIndex left, OpIndex right, MachineRepresentation rep)
      : rep(rep), inputs{left, right} {}
};

struct BitwiseAndOp : OperationT<BitwiseAndOp> {
  MachineRepresentation rep;
  OpIndex inputs[2];

  static constexpr OpProperties properties = OpProperties::Pure();

  OpIndex left() const { return inputs[0]; }
  OpIndex right() const { return inputs[1]; }

  BitwiseAndOp(OpIndex left, OpIndex right, MachineRepresentation rep)
      : rep(rep), inputs{left, right} {}
};

struct EqualOp : OperationT<EqualOp> {
  MachineRepresentation rep;
  OpIndex inputs[2];

  static constexpr OpProperties properties = OpProperties::Pure();

  OpIndex left() const { return inputs[0]; }
  OpIndex right() const { return inputs[1]; }

  EqualOp(OpIndex left, OpIndex right, MachineRepresentation rep)
      : rep(rep), inputs{left, right} {}
};

struct PhiOp : OutOfLineOpT<PhiOp> {
  struct Data {};
  using OutOfLineOpT<PhiOp>::OutOfLineOpT;
  static constexpr OpProperties properties = OpProperties::Pure();
};

struct BinaryPhiOp : OperationT<BinaryPhiOp> {
  OpIndex inputs[2];

  static constexpr OpProperties properties = OpProperties::Pure();

  OpIndex first() const { return inputs[0]; }
  OpIndex second() const { return inputs[1]; }

  BinaryPhiOp(OpIndex first, OpIndex second) : inputs{first, second} {}
};

// Only used while VarAssembler is running and the loop backedge has not been
// emitted yet.
struct PendingVariableLoopPhiOp : OperationT<PendingVariableLoopPhiOp> {
  OpIndex inputs[1];
  Variable* variable;

  static constexpr OpProperties properties = OpProperties::Pure();

  OpIndex first() const { return inputs[0]; }

  PendingVariableLoopPhiOp(OpIndex first, Variable* variable)
      : inputs{first}, variable(variable) {}
};

// Only used when moving a loop phi to a new graph while the loop backedge has
// not been emitted yet.
struct PendingLoopPhiOp : OperationT<PendingLoopPhiOp> {
  OpIndex inputs[1];
  union {
    // Used when transforming a TurboShaft graph.
    // This is not an input because it refers to the old graph.
    OpIndex old_backedge_index;
    // Used when translating from sea-of-nodes.
    Node* old_backedge_node;
  };

  static constexpr OpProperties properties = OpProperties::Pure();

  OpIndex first() const { return inputs[0]; }

  PendingLoopPhiOp(OpIndex first, OpIndex old_backedge_index)
      : inputs{first}, old_backedge_index(old_backedge_index) {
    DCHECK_NE(old_backedge_index, OpIndex::kInvalid);
  }
  PendingLoopPhiOp(OpIndex first, Node* old_backedge_node)
      : inputs{first}, old_backedge_node(old_backedge_node) {}
};

struct LoopPhiOp : OperationT<LoopPhiOp> {
  OpIndex inputs[2];
  BlockIndex backedge_block;

  static constexpr OpProperties properties = OpProperties::Pure();

  OpIndex first() const { return inputs[0]; }
  OpIndex second() const { return inputs[1]; }

  LoopPhiOp(OpIndex first, OpIndex second, BlockIndex backedge_block)
      : inputs{first, second}, backedge_block(backedge_block) {
    DCHECK_NE(backedge_block, BlockIndex::kInvalid);
  }
};

struct ConstantOp : OperationT<ConstantOp> {
  enum class Kind : uint8_t {
    kWord32,
    kWord64,
    kExternal,
    kHeapObject,
    kCompressedHeapObject
  };

  const Kind kind;
  union storage {
    uint64_t integral;
    ExternalReference external;
    Handle<HeapObject> handle;
    storage() : integral{} {}
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

  static ConstantOp Word32(uint32_t constant) {
    return ConstantOp(Kind::kWord32, constant);
  }
  static ConstantOp Word64(uint64_t constant) {
    return ConstantOp(Kind::kWord64, constant);
  }
  static ConstantOp External(ExternalReference constant) {
    return ConstantOp(constant);
  }
  static ConstantOp HeapObject(Handle<i::HeapObject> constant) {
    return ConstantOp(Kind::kHeapObject, constant);
  }
  static ConstantOp CompressedHeapObject(Handle<i::HeapObject> constant) {
    return ConstantOp(Kind::kCompressedHeapObject, constant);
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

 private:
  ConstantOp(Kind kind, uint64_t integral) : kind(kind), storage{} {
    this->storage.integral = integral;
  }
  explicit ConstantOp(ExternalReference constant) : kind(Kind::kExternal) {
    this->storage.external = constant;
  }
  explicit ConstantOp(Kind kind, Handle<i::HeapObject> constant) : kind(kind) {
    this->storage.handle = constant;
  }
};

struct LoadOp : OperationT<LoadOp> {
  enum class Kind : uint8_t { kOnHeap, kRaw };
  MachineType loaded_rep;
  OpIndex inputs[1];
  int32_t offset;
  Kind kind;

  static constexpr OpProperties properties = OpProperties::Reading();

  OpIndex base() const { return inputs[0]; }

  // Used for untagged pointers.
  static LoadOp Raw(MachineType loaded_rep, OpIndex base, int32_t offset = 0) {
    return LoadOp(Kind::kRaw, loaded_rep, base, offset);
  }
  // Used for tagged base pointer. The tag will be subtracted automatically.
  static LoadOp OnHeap(MachineType loaded_rep, OpIndex base,
                       int32_t offset = 0) {
    return LoadOp(Kind::kOnHeap, loaded_rep, base, offset);
  }

  LoadOp(Kind kind, MachineType loaded_rep, OpIndex base, int32_t offset)
      : loaded_rep(loaded_rep), inputs{base}, offset(offset), kind(kind) {}
};

struct StackPointerGreaterThanOp : OperationT<StackPointerGreaterThanOp> {
  StackCheckKind kind;
  OpIndex inputs[1];

  static constexpr OpProperties properties = OpProperties::Reading();

  OpIndex stack_limit() const { return inputs[0]; }

  StackPointerGreaterThanOp(StackCheckKind kind, OpIndex stack_limit)
      : kind(kind), inputs{stack_limit} {}
};

struct LoadStackCheckOffsetOp : OperationT<LoadStackCheckOffsetOp> {
  static constexpr OpProperties properties = OpProperties::Pure();
};

// A Checkpoint Operation describes a potential deopt point.
// Multiple check operations can go back to the same checkpoint.
// Checkpoints can depend on a preceding and dominating checkpoint to enable a
// more compact encoding of deopt information.
struct CheckpointOp : OutOfLineOpT<CheckpointOp> {
  enum class Kind {
    kFull,          // A self-contained, non-inlined checkpoint.
    kDifferential,  // Modifies a dominating checkpoint to save space.
    kInlined        // Extends a dominating checkpoint by adding a new frame.
  };

  struct Data {
    Kind kind;
    const FrameStateInfo& frame_state_info;
    // TOIMPLEMENT: StateValue information.
  };

  // TODO(tebbi): Is it pure?
  static constexpr OpProperties properties = OpProperties::Reading();

  static CheckpointOp Full(base::Vector<const OpIndex> inputs,
                           const FrameStateInfo& frame_state_info, Zone* zone) {
    CheckpointOp result =
        CheckpointOp(inputs.size(), Data{Kind::kFull, frame_state_info}, zone);
    result.Inputs().OverwriteWith(inputs);
    return result;
  }

  static CheckpointOp Differential(OpIndex base_checkpoint,
                                   base::Vector<const OpIndex> inputs,
                                   const FrameStateInfo& frame_state_info,
                                   Zone* zone) {
    CheckpointOp result = CheckpointOp(
        inputs.size() + 1, Data{Kind::kDifferential, frame_state_info}, zone);
    base::Vector<OpIndex> v = result.Inputs();
    v[0] = base_checkpoint;
    v.SubVector(1, v.size()).OverwriteWith(inputs);
    return result;
  }

 private:
  CheckpointOp(size_t input_count, Data data, Zone* zone)
      : OutOfLineOpT<CheckpointOp>(input_count, data, zone) {}
};

// CheckLazyDeoptOp should always immediately follow a call and a checkpoint.
// Semantically, it deopts to the checkpoint if the current code object has been
// deoptimized. But this might also be implemented differently.
struct CheckLazyDeoptOp : OperationT<CheckLazyDeoptOp> {
  OpIndex inputs[1];

  static constexpr OpProperties properties =
      OpProperties::NonMemorySideEffects();

  OpIndex checkpoint() const { return inputs[0]; }

  explicit CheckLazyDeoptOp(OpIndex checkpoint) : inputs{checkpoint} {}
};

struct ParameterOp : OperationT<ParameterOp> {
  uint32_t parameter_index;
  const char* debug_name;

  static constexpr OpProperties properties = OpProperties::Pure();

  explicit ParameterOp(uint32_t parameter_index, const char* debug_name = "")
      : parameter_index(parameter_index), debug_name(debug_name) {}
};

struct CallOp : OutOfLineOpT<CallOp> {
  struct Data {
    const CallDescriptor* descriptor;
  };

  static constexpr OpProperties properties = OpProperties::AnySideEffects();

  OpIndex code() const { return Inputs()[0]; }
  base::Vector<const OpIndex> arguments() const {
    return Inputs().SubVector(1, input_count);
  }

  CallOp(const CallDescriptor* descriptor, OpIndex code,
         base::Vector<const OpIndex> arguments, Zone* zone)
      : OutOfLineOpT<CallOp>(1 + arguments.size(), Data{descriptor}, zone) {
    base::Vector<OpIndex> inputs = Inputs();
    inputs[0] = code;
    inputs.SubVector(1, inputs.size()).OverwriteWith(arguments);
  }
};

struct ReturnOp : OperationT<ReturnOp> {
  OpIndex inputs[1];

  static constexpr OpProperties properties = OpProperties::BlockTerminator();

  OpIndex return_value() const { return inputs[0]; }

  explicit ReturnOp(OpIndex return_value) : inputs{return_value} {}
};

struct GotoOp : OperationT<GotoOp> {
  Block* destination;

  static constexpr OpProperties properties = OpProperties::BlockTerminator();

  explicit GotoOp(Block* destination) : destination(destination) {}
};

struct BranchOp : OutOfLineOpT<BranchOp> {
  struct Data {
    Block* if_true;
    Block* if_false;
  };

  static constexpr OpProperties properties = OpProperties::BlockTerminator();

  OpIndex condition() const { return Inputs()[0]; }
  Block* if_true() const { return data().if_true; }
  Block* if_false() const { return data().if_false; }

  explicit BranchOp(OpIndex condition, Block* if_true, Block* if_false,
                    Zone* zone)
      : OutOfLineOpT<BranchOp>(base::VectorOf({condition}),
                               Data{if_true, if_false}, zone) {}
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
    : std::integral_constant<uint32_t, sizeof(Op::inputs) / sizeof(OpIndex)> {
  STATIC_ASSERT(offsetof(Op, inputs) == Operation::kInputsOffset);
};
constexpr uint32_t kOperationInputCountTable[kNumberOfInlineOpcodes] = {
#define INPUTS_OFFSET(Name) static_operation_input_count<Name##Op>::value,
    TURBOSHAFT_INLINE_OPERATION_LIST(INPUTS_OFFSET)
#undef INPUTS_OFFSET
};

template <class Op, class = void>
struct static_operation_storage_size : std::integral_constant<size_t, 0> {};
template <class Op>
struct static_operation_storage_size<Op, base::void_t<decltype(Op::Storage)>>
    : std::integral_constant<size_t, sizeof(Op::Storage)> {};
constexpr size_t kOperationStorageSizeTable[kNumberOfInlineOpcodes] = {
#define INPUTS_OFFSET(Name) static_operation_storage_size<Name##Op>::value,
    TURBOSHAFT_INLINE_OPERATION_LIST(INPUTS_OFFSET)
#undef INPUTS_OFFSET
};

inline size_t Operation::InputCount() const {
  Opcode opcode = this->opcode;
  if (IsInlineOpcode(opcode)) {
    return kOperationInputCountTable[ToUnderlyingType(opcode)];
  } else {
    return static_cast<const OutOfLineOp*>(this)->InputCount();
  }
}

template <class Derived, class Base>
size_t OperationT<Derived, Base>::InputCount() const {
  if (IsInlineOpcode(opcode())) {
    return kOperationInputCountTable[ToUnderlyingType(opcode())];
  } else {
    return Base::InputCount();
  }
}

inline base::Vector<const OpIndex> Operation::Inputs() const {
  Opcode opcode = this->opcode;
  if (IsInlineOpcode(opcode)) {
    const OpIndex* first = reinterpret_cast<const OpIndex*>(
        reinterpret_cast<const char*>(this) + Operation::kInputsOffset);
    size_t count = kOperationInputCountTable[ToUnderlyingType(opcode)];
    return {first, count};
  } else {
    return static_cast<const OutOfLineOp*>(this)->Inputs();
  }
}

inline base::Vector<OpIndex> Operation::Inputs() {
  Opcode opcode = this->opcode;
  if (IsInlineOpcode(opcode)) {
    OpIndex* first = reinterpret_cast<OpIndex*>(reinterpret_cast<char*>(this) +
                                                Operation::kInputsOffset);
    size_t count = kOperationInputCountTable[ToUnderlyingType(opcode)];
    return {first, count};
  } else {
    return static_cast<OutOfLineOp*>(this)->Inputs();
  }
}

inline OperationStorage Operation::ReplaceInputs(
    base::Vector<const OpIndex> inputs, Zone* zone) const {
  OperationStorage result;
  memcpy(&result, this, kOperationSize);
  if (IsInlineOpcode(opcode)) {
    DCHECK_EQ(inputs.size(),
              kOperationInputCountTable[ToUnderlyingType(opcode)]);
    Operation& op = *reinterpret_cast<Operation*>(&result);
    std::copy(inputs.begin(), inputs.end(), op.Inputs().begin());
  } else {
    const OutOfLineOp& self = *static_cast<const OutOfLineOp*>(this);
    OutOfLineOp& op = *reinterpret_cast<OutOfLineOp*>(&result);

    size_t inputs_size =
        RoundUp(inputs.size() * sizeof(OpIndex), alignof(void*));
    size_t storage_size = kOperationStorageSizeTable[ToUnderlyingType(opcode)];
    OutOfLineOp::Storage* new_storage = reinterpret_cast<OutOfLineOp::Storage*>(
        static_cast<char*>(
            zone->Allocate<OutOfLineOp::Storage>(inputs_size + storage_size)) +
        inputs_size);
    memcpy(new_storage, self.storage, storage_size);
    op.input_count = static_cast<uint32_t>(inputs.size());
    memcpy(
        reinterpret_cast<char*>(new_storage) - inputs.size() * sizeof(OpIndex),
        inputs.data(), inputs.size() * sizeof(OpIndex));
    op.storage = new_storage;
  }
  return result;
}

inline OpProperties Operation::properties() const {
  return kOperationPropertiesTable[ToUnderlyingType(opcode)];
}

}  // namespace turboshaft
}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_TURBOSHAFT_OPERATIONS_H_
