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
  const uint16_t input_count;

  base::Vector<OpIndex> inputs();
  base::Vector<const OpIndex> inputs() const;

  static size_t StorageSlotCount(Opcode opcode, size_t input_count);
  size_t StorageSlotCount() const {
    return StorageSlotCount(opcode, input_count);
  }

  template <class Op>
  bool Is() const {
    return opcode == Op::opcode;
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
  explicit Operation(Opcode opcode, size_t input_count)
      : opcode(opcode), input_count(input_count) {
    DCHECK_LE(input_count,
              std::numeric_limits<decltype(this->input_count)>::max());
  }

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
  // This purposefully overshadows Operation::opcode with a static version.
  static const Opcode opcode;

  static size_t StorageSlotCount(size_t input_count) {
    constexpr size_t r = sizeof(OperationStorageSlot) / sizeof(OpIndex);
    STATIC_ASSERT(sizeof(OperationStorageSlot) % sizeof(OpIndex) == 0);
    STATIC_ASSERT(sizeof(Derived) % sizeof(OpIndex) == 0);
    size_t result = std::max<size_t>(
        2, (r - 1 + sizeof(Derived) / sizeof(OpIndex) + input_count) / r);
    DCHECK_EQ(result, Operation::StorageSlotCount(opcode, input_count));
    return result;
  }
  size_t StorageSlotCount() const { return StorageSlotCount(input_count); }

  template <class... Args>
  static Derived& New(Graph* graph, size_t input_count, Args... args) {
    OperationStorageSlot* ptr =
        AllocateOpStorage(graph, StorageSlotCount(input_count));
    Derived* result = new (ptr) Derived(std::move(args)...);
    // If this DCHECK fails, then the number of inputs specified in the
    // operation constructor and in the static New function disagree.
    DCHECK_EQ(input_count, result->Operation::input_count);
    return *result;
  }

  template <class... Args>
  static Derived& New(Graph* graph, base::Vector<const OpIndex> inputs,
                      Args... args) {
    return New(graph, inputs.size(), inputs, args...);
  }

  explicit OperationT(size_t input_count) : Operation(opcode, input_count) {
    STATIC_ASSERT((std::is_base_of<OperationT, Derived>::value));
    STATIC_ASSERT(std::is_trivially_copyable<Derived>::value);
    STATIC_ASSERT(std::is_trivially_destructible<Derived>::value);
  }
  explicit OperationT(base::Vector<const OpIndex> inputs)
      : Operation(opcode, inputs.size()) {
    this->inputs().OverwriteWith(inputs);
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

  explicit FixedOperationT(base::Array<OpIndex, InputCount> inputs)
      : OperationT<Derived>(InputCount) {
    this->inputs().OverwriteWith(inputs);
  }

  // Redefine the input initialization to tell C++ about the static input size.
  template <class... Args>
  static Derived& New(Graph* graph, Args... args) {
    Derived& result =
        OperationT<Derived>::New(graph, InputCount, std::move(args)...);
    return result;
  }
};

struct AddOp : FixedOperationT<2, AddOp> {
  MachineRepresentation rep;

  static constexpr OpProperties properties = OpProperties::Pure();

  OpIndex left() const { return inputs()[0]; }
  OpIndex right() const { return inputs()[1]; }

  AddOp(OpIndex left, OpIndex right, MachineRepresentation rep)
      : Base({left, right}), rep(rep) {}
};

struct SubOp : FixedOperationT<2, SubOp> {
  MachineRepresentation rep;

  static constexpr OpProperties properties = OpProperties::Pure();

  OpIndex left() const { return inputs()[0]; }
  OpIndex right() const { return inputs()[1]; }

  SubOp(OpIndex left, OpIndex right, MachineRepresentation rep)
      : Base({left, right}), rep(rep) {}
};

struct BitwiseAndOp : FixedOperationT<2, BitwiseAndOp> {
  MachineRepresentation rep;

  static constexpr OpProperties properties = OpProperties::Pure();

  OpIndex left() const { return inputs()[0]; }
  OpIndex right() const { return inputs()[1]; }

  BitwiseAndOp(OpIndex left, OpIndex right, MachineRepresentation rep)
      : Base({left, right}), rep(rep) {}
};

struct EqualOp : FixedOperationT<2, EqualOp> {
  MachineRepresentation rep;

  static constexpr OpProperties properties = OpProperties::Pure();

  OpIndex left() const { return inputs()[0]; }
  OpIndex right() const { return inputs()[1]; }

  EqualOp(OpIndex left, OpIndex right, MachineRepresentation rep)
      : Base({left, right}), rep(rep) {}
};

struct PhiOp : OperationT<PhiOp> {
  MachineRepresentation rep;

  static constexpr OpProperties properties = OpProperties::Pure();

  explicit PhiOp(base::Vector<const OpIndex> inputs, MachineRepresentation rep)
      : Base(inputs) {}

  static PhiOp& New(Graph* graph, base::Vector<const OpIndex> inputs,
                    MachineRepresentation rep) {
    return Base::New(graph, inputs, rep);
  }
};

// Only used while VarAssembler is running and the loop backedge has not been
// emitted yet.
struct PendingVariableLoopPhiOp : FixedOperationT<1, PendingVariableLoopPhiOp> {
  Variable* variable;

  static constexpr OpProperties properties = OpProperties::Pure();

  OpIndex first() const { return inputs()[0]; }

  PendingVariableLoopPhiOp(OpIndex first, Variable* variable)
      : Base({first}), variable(variable) {}
};

// Only used when moving a loop phi to a new graph while the loop backedge has
// not been emitted yet.
struct PendingLoopPhiOp : FixedOperationT<1, PendingLoopPhiOp> {
  MachineRepresentation rep;
  union {
    // Used when transforming a TurboShaft graph.
    // This is not an input because it refers to the old graph.
    OpIndex old_backedge_index = OpIndex::Invalid();
    // Used when translating from sea-of-nodes.
    Node* old_backedge_node;
  };

  static constexpr OpProperties properties = OpProperties::Pure();

  OpIndex first() const { return inputs()[0]; }

  PendingLoopPhiOp(OpIndex first, MachineRepresentation rep,
                   OpIndex old_backedge_index)
      : Base({first}), rep(rep), old_backedge_index(old_backedge_index) {
    DCHECK(old_backedge_index.valid());
  }
  PendingLoopPhiOp(OpIndex first, MachineRepresentation rep,
                   Node* old_backedge_node)
      : Base({first}), rep(rep), old_backedge_node(old_backedge_node) {}
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
    return Base::New(graph, Kind::kWord32, constant);
  }
  static ConstantOp& NewWord64(Graph* graph, uint64_t constant) {
    return Base::New(graph, Kind::kWord64, constant);
  }
  static ConstantOp& NewExternal(Graph* graph, ExternalReference constant) {
    return Base::New(graph, Kind::kExternal, constant);
  }
  static ConstantOp& NewHeapObject(Graph* graph,
                                   Handle<i::HeapObject> constant) {
    return Base::New(graph, Kind::kHeapObject, constant);
  }
  static ConstantOp& NewCompressedHeapObject(Graph* graph,
                                             Handle<i::HeapObject> constant) {
    return Base::New(graph, Kind::kCompressedHeapObject, constant);
  }

  ConstantOp(Kind kind, Storage storage)
      : Base({}), kind(kind), storage(storage) {}

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
  Kind kind;
  MachineType loaded_rep;
  int32_t offset;

  static constexpr OpProperties properties = OpProperties::Reading();

  OpIndex base() const { return inputs()[0]; }

  LoadOp(OpIndex base, Kind kind, MachineType loaded_rep, int32_t offset = 0)
      : Base({base}), kind(kind), loaded_rep(loaded_rep), offset(offset) {}
};

struct StackPointerGreaterThanOp
    : FixedOperationT<1, StackPointerGreaterThanOp> {
  StackCheckKind kind;

  static constexpr OpProperties properties = OpProperties::Reading();

  OpIndex stack_limit() const { return inputs()[0]; }

  StackPointerGreaterThanOp(OpIndex stack_limit, StackCheckKind kind)
      : Base({stack_limit}), kind(kind) {}
};

struct LoadStackCheckOffsetOp : FixedOperationT<0, LoadStackCheckOffsetOp> {
  static constexpr OpProperties properties = OpProperties::Pure();

  LoadStackCheckOffsetOp() : Base({}) {}
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

  explicit CheckpointOp(base::Vector<const OpIndex> inputs) : Base(inputs) {}

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
  // }s
};

// CheckLazyDeoptOp should always immediately follow a call and a checkpoint.
// Semantically, it deopts to the checkpoint if the current code object has been
// deoptimized. But this might also be implemented differently.
struct CheckLazyDeoptOp : FixedOperationT<1, CheckLazyDeoptOp> {
  static constexpr OpProperties properties =
      OpProperties::NonMemorySideEffects();

  OpIndex checkpoint() const { return inputs()[0]; }

  explicit CheckLazyDeoptOp(OpIndex checkpoint) : Base({checkpoint}) {}
};

struct ParameterOp : FixedOperationT<0, ParameterOp> {
  uint32_t parameter_index;
  const char* debug_name;

  static constexpr OpProperties properties = OpProperties::Pure();

  explicit ParameterOp(uint32_t parameter_index, const char* debug_name = "")
      : Base({}), parameter_index(parameter_index), debug_name(debug_name) {}
};

struct CallOp : OperationT<CallOp> {
  const CallDescriptor* descriptor;

  static constexpr OpProperties properties = OpProperties::AnySideEffects();

  OpIndex callee() const { return inputs()[0]; }
  base::Vector<const OpIndex> arguments() const {
    return inputs().SubVector(1, input_count);
  }

  CallOp(OpIndex callee, base::Vector<const OpIndex> arguments,
         const CallDescriptor* descriptor)
      : Base(1 + arguments.size()), descriptor(descriptor) {
    base::Vector<OpIndex> inputs = this->inputs();
    inputs[0] = callee;
    inputs.SubVector(1, inputs.size()).OverwriteWith(arguments);
  }
  static CallOp& New(Graph* graph, OpIndex callee,
                     base::Vector<const OpIndex> arguments,
                     const CallDescriptor* descriptor) {
    return Base::New(graph, 1 + arguments.size(), callee, arguments,
                     descriptor);
  }
};

struct ReturnOp : OperationT<ReturnOp> {
  int32_t pop_count;

  static constexpr OpProperties properties = OpProperties::BlockTerminator();

  base::Vector<const OpIndex> return_values() const { return inputs(); }

  ReturnOp(base::Vector<const OpIndex> return_values, int32_t pop_count)
      : Base(return_values), pop_count(pop_count) {}
};

struct GotoOp : FixedOperationT<0, GotoOp> {
  Block* destination;

  static constexpr OpProperties properties = OpProperties::BlockTerminator();

  explicit GotoOp(Block* destination) : Base({}), destination(destination) {}
};

struct BranchOp : FixedOperationT<1, BranchOp> {
  Block* if_true;
  Block* if_false;

  static constexpr OpProperties properties = OpProperties::BlockTerminator();

  OpIndex condition() const { return inputs()[0]; }

  BranchOp(OpIndex condition, Block* if_true, Block* if_false)
      : Base({condition}), if_true(if_true), if_false(if_false) {}
};

#define OPERATION_PROPERTIES_CASE(Name) Name##Op::properties,
static constexpr OpProperties kOperationPropertiesTable[kNumberOfOpcodes] = {
    TURBOSHAFT_OPERATION_LIST(OPERATION_PROPERTIES_CASE)};
#undef OPERATION_PROPERTIES_CASE

template <class Op>
struct operation_to_opcode_map {};

#define OPERATION_OPCODE_MAP_CASE(Name)    \
  template <>                              \
  struct operation_to_opcode_map<Name##Op> \
      : std::integral_constant<Opcode, Opcode::k##Name> {};
TURBOSHAFT_OPERATION_LIST(OPERATION_OPCODE_MAP_CASE)
#undef OPERATION_OPCODE_MAP_CASE

template <class Op>
const Opcode OperationT<Op>::opcode = operation_to_opcode_map<Op>::value;

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
