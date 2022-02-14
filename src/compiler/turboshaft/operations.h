// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_OPERATIONS_H_
#define V8_COMPILER_TURBOSHAFT_OPERATIONS_H_

#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>

#include "src/base/macros.h"
#include "src/base/small-vector.h"
#include "src/base/vector.h"
#include "src/codegen/external-reference.h"
#include "src/codegen/machine-type.h"
#include "src/codegen/tnode.h"
#include "src/common/globals.h"
#include "src/compiler/backend/instruction-codes.h"
#include "src/compiler/globals.h"
#include "src/compiler/write-barrier-kind.h"
#include "src/zone/zone.h"

namespace v8 {
namespace internal {

class HeapObject;
class StringConstantBase;

namespace compiler {

class CallDescriptor;
class Node;
class FrameStateInfo;
class DeoptimizeParameters;

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
  V(Mul)                             \
  V(FloatUnary)                      \
  V(BitwiseAnd)                      \
  V(BitwiseOr)                       \
  V(BitwiseXor)                      \
  V(Shift)                           \
  V(Equal)                           \
  V(Comparison)                      \
  V(Change)                          \
  V(TaggedBitcast)                   \
  V(PendingVariableLoopPhi)          \
  V(PendingLoopPhi)                  \
  V(Constant)                        \
  V(Load)                            \
  V(IndexedLoad)                     \
  V(Store)                           \
  V(IndexedStore)                    \
  V(Parameter)                       \
  V(Goto)                            \
  V(StackPointerGreaterThan)         \
  V(LoadStackCheckOffset)            \
  V(CheckLazyDeopt)                  \
  V(Deoptimize)                      \
  V(DeoptimizeIf)                    \
  V(Phi)                             \
  V(FrameState)                      \
  V(Call)                            \
  V(Unreachable)                     \
  V(Return)                          \
  V(Branch)                          \
  V(Switch)                          \
  V(Projection)

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

std::ostream& operator<<(std::ostream& os, const Operation& op);

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

  void PrintOptions(std::ostream& os) const {}
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
  enum class Kind : uint8_t {
    kWithOverflowBit,
    kWithoutOverflowBit,
  };
  Kind kind;
  MachineRepresentation rep;

  static constexpr OpProperties properties = OpProperties::Pure();

  OpIndex left() const { return inputs()[0]; }
  OpIndex right() const { return inputs()[1]; }

  AddOp(OpIndex left, OpIndex right, Kind kind, MachineRepresentation rep)
      : Base({left, right}), kind(kind), rep(rep) {}
  void PrintOptions(std::ostream& os) const;
};

struct SubOp : FixedOperationT<2, SubOp> {
  enum class Kind : uint8_t {
    kWithOverflowBit,
    kWithoutOverflowBit,
  };
  Kind kind;
  MachineRepresentation rep;

  static constexpr OpProperties properties = OpProperties::Pure();

  OpIndex left() const { return inputs()[0]; }
  OpIndex right() const { return inputs()[1]; }

  SubOp(OpIndex left, OpIndex right, Kind kind, MachineRepresentation rep)
      : Base({left, right}), kind(kind), rep(rep) {}
  void PrintOptions(std::ostream& os) const;
};

struct MulOp : FixedOperationT<2, MulOp> {
  enum class Kind : uint8_t {
    kWithOverflowBit,
    kWithoutOverflowBit,
  };
  Kind kind;
  MachineRepresentation rep;

  static constexpr OpProperties properties = OpProperties::Pure();

  OpIndex left() const { return inputs()[0]; }
  OpIndex right() const { return inputs()[1]; }

  MulOp(OpIndex left, OpIndex right, Kind kind, MachineRepresentation rep)
      : Base({left, right}), kind(kind), rep(rep) {}
};

struct FloatUnaryOp : FixedOperationT<1, FloatUnaryOp> {
  enum class Kind : uint8_t { kAbs, kNegate, kSilenceNaN };
  Kind kind;
  MachineRepresentation rep;
  static constexpr OpProperties properties = OpProperties::Pure();

  OpIndex input() const { return inputs()[0]; }

  explicit FloatUnaryOp(OpIndex input, Kind kind, MachineRepresentation rep)
      : Base({input}), kind(kind), rep(rep) {}
};

struct BitwiseAndOp : FixedOperationT<2, BitwiseAndOp> {
  MachineRepresentation rep;

  static constexpr OpProperties properties = OpProperties::Pure();

  OpIndex left() const { return inputs()[0]; }
  OpIndex right() const { return inputs()[1]; }

  BitwiseAndOp(OpIndex left, OpIndex right, MachineRepresentation rep)
      : Base({left, right}), rep(rep) {}
};

struct BitwiseOrOp : FixedOperationT<2, BitwiseOrOp> {
  MachineRepresentation rep;

  static constexpr OpProperties properties = OpProperties::Pure();

  OpIndex left() const { return inputs()[0]; }
  OpIndex right() const { return inputs()[1]; }

  BitwiseOrOp(OpIndex left, OpIndex right, MachineRepresentation rep)
      : Base({left, right}), rep(rep) {}
};

struct BitwiseXorOp : FixedOperationT<2, BitwiseXorOp> {
  MachineRepresentation rep;

  static constexpr OpProperties properties = OpProperties::Pure();

  OpIndex left() const { return inputs()[0]; }
  OpIndex right() const { return inputs()[1]; }

  BitwiseXorOp(OpIndex left, OpIndex right, MachineRepresentation rep)
      : Base({left, right}), rep(rep) {}
};

struct ShiftOp : FixedOperationT<2, ShiftOp> {
  enum class Kind : uint8_t {
    kShiftRightArithmeticShiftOutZeros,
    kShiftRightArithmetic,
    kShiftRightLogical,
    kShiftLeft
  };
  Kind kind;
  MachineRepresentation rep;

  static constexpr OpProperties properties = OpProperties::Pure();

  OpIndex left() const { return inputs()[0]; }
  OpIndex right() const { return inputs()[1]; }

  ShiftOp(OpIndex left, OpIndex right, Kind kind, MachineRepresentation rep)
      : Base({left, right}), kind(kind), rep(rep) {}
};

struct EqualOp : FixedOperationT<2, EqualOp> {
  MachineRepresentation rep;

  static constexpr OpProperties properties = OpProperties::Pure();

  OpIndex left() const { return inputs()[0]; }
  OpIndex right() const { return inputs()[1]; }

  EqualOp(OpIndex left, OpIndex right, MachineRepresentation rep)
      : Base({left, right}), rep(rep) {}
};

struct ComparisonOp : FixedOperationT<2, ComparisonOp> {
  enum class Kind : uint8_t {
    kSignedLessThan,
    kSignedLessThanOrEqual,
    kUnsignedLessThan,
    kUnsignedLessThanOrEqual
  };
  Kind kind;
  MachineRepresentation rep;

  static constexpr OpProperties properties = OpProperties::Pure();

  OpIndex left() const { return inputs()[0]; }
  OpIndex right() const { return inputs()[1]; }

  ComparisonOp(OpIndex left, OpIndex right, Kind kind,
               MachineRepresentation rep)
      : Base({left, right}), kind(kind), rep(rep) {}
};

struct ChangeOp : FixedOperationT<1, ChangeOp> {
  enum class Kind : uint8_t {
    // narrowing means undefined behavior if value cannot be represented
    // precisely
    kSignedNarrowing,
    kUnsignedNarrowing,
    // reduce integer bit-width, resulting in a modulo operation
    kIntegerTruncate,
    // system-specific conversion to (un)signed number
    kSignedFloatTruncate,
    kUnsignedFloatTruncate,
    // like kSignedFloatTruncate, but overflow guaranteed to result in the
    // minimal integer
    kSignedFloatTruncateOverflowToMin,
    // extract half of a float64 value
    kExtractHighHalf,
    kExtractLowHalf,
    // increase bit-width for unsigned integer values
    kZeroExtend,
    // increase bid-width for signed integer values
    kSignExtend,
    // preserve bits, change meaning
    kBitcast
  };
  Kind kind;
  MachineRepresentation from;
  MachineRepresentation to;

  static constexpr OpProperties properties = OpProperties::Pure();

  OpIndex input() const { return inputs()[0]; }

  ChangeOp(OpIndex input, Kind kind, MachineRepresentation from,
           MachineRepresentation to)
      : Base({input}), kind(kind), from(from), to(to) {}
};

struct TaggedBitcastOp : FixedOperationT<1, TaggedBitcastOp> {
  MachineRepresentation from;
  MachineRepresentation to;

  // Due to moving GC, converting from or to pointers doesn't commute with GC.
  static constexpr OpProperties properties = OpProperties::Reading();

  OpIndex input() const { return inputs()[0]; }

  TaggedBitcastOp(OpIndex input, MachineRepresentation from,
                  MachineRepresentation to)
      : Base({input}), from(from), to(to) {}
};

struct PhiOp : OperationT<PhiOp> {
  MachineRepresentation rep;

  static constexpr OpProperties properties = OpProperties::Pure();

  explicit PhiOp(base::Vector<const OpIndex> inputs, MachineRepresentation rep)
      : Base(inputs), rep(rep) {}

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
    kFloat32,
    kFloat64,
    kNumber,  // TODO(tebbi): See if we can avoid number constants.
    kTaggedIndex,
    kExternal,
    kHeapObject,
    kCompressedHeapObject,
    kDelayedString
  };

  Kind kind;
  union Storage {
    uint64_t integral;
    float float32;
    double float64;
    ExternalReference external;
    Handle<HeapObject> handle;
    const StringConstantBase* string;

    Storage(uint64_t integral = 0) : integral(integral) {}
    Storage(double constant) : float64(constant) {}
    Storage(float constant) : float32(constant) {}
    Storage(ExternalReference constant) : external(constant) {}
    Storage(Handle<HeapObject> constant) : handle(constant) {}
    Storage(const StringConstantBase* constant) : string(constant) {}
  } storage;

  static constexpr OpProperties properties = OpProperties::Pure();

  MachineRepresentation Representation() const {
    switch (kind) {
      case Kind::kWord32:
        return MachineRepresentation::kWord32;
      case Kind::kWord64:
        return MachineRepresentation::kWord64;
      case Kind::kFloat32:
        return MachineRepresentation::kFloat32;
      case Kind::kFloat64:
        return MachineRepresentation::kFloat64;
      case Kind::kExternal:
      case Kind::kTaggedIndex:
        return MachineType::PointerRepresentation();
      case Kind::kHeapObject:
      case Kind::kNumber:
      case Kind::kDelayedString:
        return MachineRepresentation::kTagged;
      case Kind::kCompressedHeapObject:
        return MachineRepresentation::kCompressed;
    }
  }

  static ConstantOp& NewWord32(Graph* graph, uint32_t constant) {
    return Base::New(graph, Kind::kWord32, uint64_t{constant});
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

  double number() const {
    DCHECK_EQ(kind, Kind::kNumber);
    return storage.float64;
  }

  float float32() const {
    DCHECK_EQ(kind, Kind::kFloat32);
    return storage.float32;
  }

  double float64() const {
    DCHECK_EQ(kind, Kind::kFloat64);
    return storage.float64;
  }

  int32_t tagged_index() const {
    DCHECK_EQ(kind, Kind::kTaggedIndex);
    return static_cast<int32_t>(static_cast<uint32_t>(storage.integral));
  }

  ExternalReference external_reference() const {
    DCHECK_EQ(kind, Kind::kExternal);
    return storage.external;
  }

  Handle<i::HeapObject> handle() const {
    DCHECK(kind == Kind::kHeapObject || kind == Kind::kCompressedHeapObject);
    return storage.handle;
  }

  const StringConstantBase* delayed_string() const {
    DCHECK(kind == Kind::kDelayedString);
    return storage.string;
  }

  void PrintOptions(std::ostream& os) const;
};

struct LoadOp : FixedOperationT<1, LoadOp> {
  enum class Kind : uint8_t { kOnHeap, kRaw };
  Kind kind;
  MachineType loaded_rep;
  int32_t offset;

  static constexpr OpProperties properties = OpProperties::Reading();

  OpIndex base() const { return inputs()[0]; }

  LoadOp(OpIndex base, Kind kind, MachineType loaded_rep, int32_t offset)
      : Base({base}), kind(kind), loaded_rep(loaded_rep), offset(offset) {}
  void PrintOptions(std::ostream& os) const;
};

struct IndexedLoadOp : FixedOperationT<2, IndexedLoadOp> {
  enum class Kind : uint8_t { kOnHeap, kRaw };
  Kind kind;
  MachineType loaded_rep;
  uint8_t element_scale;  // multiply index with 2^element_scale
  int32_t offset;         // add offset to index

  static constexpr OpProperties properties = OpProperties::Reading();

  OpIndex base() const { return inputs()[0]; }
  OpIndex index() const { return inputs()[1]; }

  IndexedLoadOp(OpIndex base, OpIndex index, Kind kind, MachineType loaded_rep,
                int32_t offset, uint8_t element_scale)
      : Base({base, index}),
        kind(kind),
        loaded_rep(loaded_rep),
        element_scale(element_scale),
        offset(offset) {}
};

struct StoreOp : FixedOperationT<2, StoreOp> {
  enum class Kind : uint8_t { kOnHeap, kRaw };
  Kind kind;
  MachineRepresentation stored_rep;
  WriteBarrierKind write_barrier;
  int32_t offset;

  static constexpr OpProperties properties = OpProperties::Writing();

  OpIndex base() const { return inputs()[0]; }
  OpIndex value() const { return inputs()[1]; }

  StoreOp(OpIndex base, OpIndex value, Kind kind,
          MachineRepresentation stored_rep, WriteBarrierKind write_barrier,
          int32_t offset)
      : Base({base, value}),
        kind(kind),
        stored_rep(stored_rep),
        write_barrier(write_barrier),
        offset(offset) {}
  void PrintOptions(std::ostream& os) const;
};

struct IndexedStoreOp : FixedOperationT<3, IndexedStoreOp> {
  enum class Kind : uint8_t { kOnHeap, kRaw };
  Kind kind;
  MachineRepresentation stored_rep;
  WriteBarrierKind write_barrier;
  uint8_t element_scale;  // multiply index with 2^element_scale
  int32_t offset;         // add offset to index

  static constexpr OpProperties properties = OpProperties::Writing();

  OpIndex base() const { return inputs()[0]; }
  OpIndex index() const { return inputs()[1]; }
  OpIndex value() const { return inputs()[2]; }

  IndexedStoreOp(OpIndex base, OpIndex index, OpIndex value, Kind kind,
                 MachineRepresentation stored_rep,
                 WriteBarrierKind write_barrier, int32_t offset,
                 uint8_t element_scale)
      : Base({base, index, value}),
        kind(kind),
        stored_rep(stored_rep),
        write_barrier(write_barrier),
        element_scale(element_scale),
        offset(offset) {}
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

struct FrameStateData {
  // The data is encoded as a pre-traversal of a tree.
  enum class Instr : uint8_t {
    kInput,  // 1 Operand: input machine type
    kUnusedRegister,
    kDematerializedObject,          // 2 Operands: id, field_count
    kDematerializedObjectReference  // 1 Operand: id
  };

  class Builder {
   public:
    void AddParentFrameState(OpIndex parent) {
      DCHECK(inputs_.empty());
      inlined_ = true;
      inputs_.push_back(parent);
    }
    void AddInput(MachineType type, OpIndex input) {
      instructions_.push_back(Instr::kInput);
      machine_types_.push_back(type);
      inputs_.push_back(input);
    }

    void AddUnusedRegister() {
      instructions_.push_back(Instr::kUnusedRegister);
    }

    void AddDematerializedObjectReference(uint32_t id) {
      instructions_.push_back(Instr::kDematerializedObjectReference);
      int_operands_.push_back(id);
    }

    void AddDematerializedObject(uint32_t id, uint32_t field_count) {
      instructions_.push_back(Instr::kDematerializedObject);
      int_operands_.push_back(id);
      int_operands_.push_back(field_count);
    }

    const FrameStateData* AllocateFrameStateData(
        const FrameStateInfo& frame_state_info, Zone* zone) {
      return zone->New<FrameStateData>(FrameStateData{
          frame_state_info, zone->CloneVector(base::VectorOf(instructions_)),
          zone->CloneVector(base::VectorOf(machine_types_)),
          zone->CloneVector(base::VectorOf(int_operands_))});
    }

    base::Vector<const OpIndex> Inputs() { return base::VectorOf(inputs_); }
    bool inlined() const { return inlined_; }

   private:
    base::SmallVector<Instr, 32> instructions_;
    base::SmallVector<MachineType, 32> machine_types_;
    base::SmallVector<uint32_t, 16> int_operands_;
    base::SmallVector<OpIndex, 32> inputs_;
    bool inlined_ = false;
  };

  struct Iterator {
    base::Vector<const Instr> instructions;
    base::Vector<const MachineType> machine_types;
    base::Vector<const uint32_t> int_operands;
    base::Vector<const OpIndex> inputs;

    bool has_more() const { return !instructions.empty(); }

    Instr current_instr() { return instructions[0]; }

    void ConsumeInput(MachineType* machine_type, OpIndex* input) {
      DCHECK(instructions[0] == Instr::kInput);
      instructions += 1;
      *machine_type = machine_types[0];
      machine_types += 1;
      *input = inputs[0];
      inputs += 1;
    }
    void ConsumeUnusedRegister() {
      DCHECK(instructions[0] == Instr::kUnusedRegister);
      instructions += 1;
    }
    void ConsumeDematerializedObject(uint32_t* id, uint32_t* field_count) {
      DCHECK(instructions[0] == Instr::kDematerializedObject);
      instructions += 1;
      *id = int_operands[0];
      *field_count = int_operands[1];
      int_operands += 1;
    }
    void ConsumeDematerializedObjectReference(uint32_t* id) {
      DCHECK(instructions[0] == Instr::kDematerializedObjectReference);
      instructions += 1;
      *id = int_operands[0];
      int_operands += 1;
    }
  };

  Iterator iterator(base::Vector<const OpIndex> state_values) const {
    return Iterator{instructions, machine_types, int_operands, state_values};
  }

  const FrameStateInfo& frame_state_info;
  base::Vector<Instr> instructions;
  base::Vector<MachineType> machine_types;
  base::Vector<uint32_t> int_operands;
};

struct FrameStateOp : OperationT<FrameStateOp> {
  bool inlined;
  const FrameStateData* data;

  static constexpr OpProperties properties = OpProperties::Reading();

  OpIndex parent_frame_state() const {
    DCHECK(inlined);
    return inputs()[0];
  }
  base::Vector<const OpIndex> state_values() const {
    base::Vector<const OpIndex> result = inputs();
    if (inlined) result += 1;
    return result;
  }

  explicit FrameStateOp(base::Vector<const OpIndex> inputs, bool inlined,
                        const FrameStateData* data)
      : Base(inputs), inlined(inlined), data(data) {}
  void PrintOptions(std::ostream& os) const;
};

// CheckLazyDeoptOp should always immediately follow a call.
// Semantically, it deopts if the current code object has been
// deoptimized. But this might also be implemented differently.
struct CheckLazyDeoptOp : FixedOperationT<2, CheckLazyDeoptOp> {
  static constexpr OpProperties properties =
      OpProperties::NonMemorySideEffects();

  OpIndex call() const { return inputs()[0]; }
  OpIndex frame_state() const { return inputs()[1]; }

  CheckLazyDeoptOp(OpIndex call, OpIndex frame_state)
      : Base({call, frame_state}) {}
};

struct DeoptimizeOp : FixedOperationT<1, DeoptimizeOp> {
  const DeoptimizeParameters* parameters;

  static constexpr OpProperties properties = OpProperties::BlockTerminator();

  OpIndex frame_state() const { return inputs()[0]; }

  DeoptimizeOp(OpIndex frame_state, const DeoptimizeParameters* parameters)
      : Base({frame_state}), parameters(parameters) {}
};

struct DeoptimizeIfOp : FixedOperationT<2, DeoptimizeIfOp> {
  bool negated;
  const DeoptimizeParameters* parameters;

  static constexpr OpProperties properties =
      OpProperties::NonMemorySideEffects();

  OpIndex condition() const { return inputs()[0]; }
  OpIndex frame_state() const { return inputs()[1]; }

  DeoptimizeIfOp(OpIndex condition, OpIndex frame_state, bool negated,
                 const DeoptimizeParameters* parameters)
      : Base({condition, frame_state}),
        negated(negated),
        parameters(parameters) {}
};

struct ParameterOp : FixedOperationT<0, ParameterOp> {
  int32_t parameter_index;
  const char* debug_name;

  static constexpr OpProperties properties = OpProperties::Pure();

  explicit ParameterOp(int32_t parameter_index, const char* debug_name = "")
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

struct UnreachableOp : FixedOperationT<0, UnreachableOp> {
  static constexpr OpProperties properties = OpProperties::BlockTerminator();

  UnreachableOp() : Base({}) {}
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

struct SwitchOp : FixedOperationT<1, SwitchOp> {
  struct Case {
    int32_t value;
    Block* destination;

    Case(int32_t value, Block* destination)
        : value(value), destination(destination) {}
  };
  base::Vector<const Case> cases;
  Block* default_case;

  static constexpr OpProperties properties = OpProperties::BlockTerminator();

  OpIndex input() const { return inputs()[0]; }

  SwitchOp(OpIndex input, base::Vector<const Case> cases, Block* default_case)
      : Base({input}), cases(cases), default_case(default_case) {}
};

struct ProjectionOp : FixedOperationT<1, ProjectionOp> {
  enum class Kind { kOverflowBit };
  Kind kind;

  static constexpr OpProperties properties = OpProperties::Pure();

  OpIndex input() const { return inputs()[0]; }

  ProjectionOp(OpIndex input, Kind kind) : Base({input}), kind(kind) {}
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
