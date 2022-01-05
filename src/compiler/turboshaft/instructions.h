// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_INSTRUCTIONS_H_
#define V8_COMPILER_TURBOSHAFT_INSTRUCTIONS_H_

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

#define INLINE_INSTRUCTION_LIST(V) \
  V(Add)                           \
  V(Sub)                           \
  V(BinaryPhi)                     \
  V(LoopPhi)                       \
  V(PendingVariableLoopPhi)        \
  V(PendingLoopPhi)                \
  V(Constant)                      \
  V(Load)                          \
  V(Parameter)                     \
  V(Return)                        \
  V(Goto)                          \
  V(StackPointerGreaterThan)       \
  V(LoadStackCheckOffset)          \
  V(CheckLazyDeopt)

#define OUT_OF_LINE_INSTRUCTION_LIST(V) \
  V(Phi)                                \
  V(Checkpoint)                         \
  V(Call)                               \
  V(Branch)

#define INSTRUCTION_LIST(V)  \
  INLINE_INSTRUCTION_LIST(V) \
  OUT_OF_LINE_INSTRUCTION_LIST(V)

enum class Opcode : uint16_t {
#define ENUM_CONSTANT(Name) k##Name,
  INLINE_INSTRUCTION_LIST(ENUM_CONSTANT) kNumberOfInlineOpcodes,
  kLastInlineOpcode = kNumberOfInlineOpcodes - 1,
  OUT_OF_LINE_INSTRUCTION_LIST(ENUM_CONSTANT) kNumberOfOpcodes
#undef ENUM_CONSTANT
};

constexpr uint16_t kNumberOfInlineOpcodes =
    ToUnderlyingType(Opcode::kNumberOfInlineOpcodes);
constexpr uint16_t kNumberOfOpcodes =
    ToUnderlyingType(Opcode::kNumberOfOpcodes);

// Use enum class to prevent implicit conversions.

enum class InstrIndex : uint32_t {
  kInvalid = std::numeric_limits<uint32_t>::max()
};

enum class BlockIndex : uint32_t {
  kInvalid = std::numeric_limits<uint32_t>::max()
};

// All instructions can be stored with 16 bytes.
static constexpr size_t kInstructionSize = 16;
using InstructionStorage =
    std::aligned_storage_t<kInstructionSize, alignof(void*)>;
STATIC_ASSERT(sizeof(InstructionStorage) == kInstructionSize);

struct Instruction {
  Opcode opcode;

  template <class Instr>
  struct opcode_of {};

  template <class Instr>
  bool Is() const {
    return opcode == Instr::opcode();
  }
  template <class Instr>
  Instr& Cast() {
    DCHECK(Is<Instr>());
    return *static_cast<Instr*>(this);
  }
  template <class Instr>
  const Instr& Cast() const {
    DCHECK(Is<Instr>());
    return *static_cast<const Instr*>(this);
  }

  base::Vector<const InstrIndex> Inputs() const;
  base::Vector<InstrIndex> Inputs();
  size_t InputCount() const;
  InstructionStorage ReplaceInputs(base::Vector<const InstrIndex> inputs,
                                   Zone* zone) const;
  bool IsReading() const;
  bool IsWriting() const;
  bool IsRequiredWhenUnused() const;

  explicit Instruction(Opcode opcode) : opcode(opcode) {}
  static constexpr size_t kInputsOffset = 4;
};

template <class Derived, class Base = Instruction>
struct InstructionT : Base {
  template <class... Args>
  explicit InstructionT(Args... args) : Base(opcode(), args...) {
    STATIC_ASSERT((std::is_base_of<InstructionT, Derived>::value));
    STATIC_ASSERT(alignof(Derived) <= alignof(InstructionStorage));
    STATIC_ASSERT(sizeof(Derived) <= sizeof(InstructionStorage));
    STATIC_ASSERT(std::is_trivially_copyable<Derived>::value);
    STATIC_ASSERT(std::is_trivially_destructible<Derived>::value);
  }
  static constexpr bool IsReading() { return Derived::is_reading; }
  static constexpr bool IsWriting() { return Derived::is_writing; }
  static constexpr bool IsBlockTerminator() {
    return Derived::is_block_terminator;
  }
  static constexpr bool IsRequiredWhenUnused() {
    return IsWriting() || IsBlockTerminator();
  }
  // This purposefully overshadows Instruction::opcode, because it's better to
  // use the static value when we already know the instruction type.
  static constexpr Opcode opcode() {
    return Instruction::opcode_of<Derived>::value;
  }

  size_t InputCount() const;
};

struct OutOfLineInstr : Instruction {
  struct Storage {};
  uint32_t input_count;
  Storage* storage;

  base::Vector<const InstrIndex> Inputs() const {
    return {reinterpret_cast<const InstrIndex*>(
                reinterpret_cast<const char*>(storage) -
                input_count * sizeof(InstrIndex)),
            input_count};
  }
  base::Vector<InstrIndex> Inputs() {
    return {reinterpret_cast<InstrIndex*>(reinterpret_cast<char*>(storage) -
                                          input_count * sizeof(InstrIndex)),
            input_count};
  }
  size_t InputCount() const { return input_count; }

  explicit OutOfLineInstr(Opcode opcode, uint32_t input_count)
      : Instruction(opcode), input_count(input_count) {}
};

template <class Derived>
struct OutOfLineInstrT : InstructionT<Derived, OutOfLineInstr> {
  struct Storage : OutOfLineInstr::Storage {
    using Data = typename Derived::Data;
    Data data;

    explicit Storage(const Data& data) : data(data) {
      STATIC_ASSERT(std::is_trivially_destructible<Storage>::value);
      STATIC_ASSERT(std::is_trivially_copyable<Storage>::value);
      // Necessary to generically copy Storage in Instruction::ReplaceInputs().
      STATIC_ASSERT(alignof(Storage) <= alignof(void*));
    }
  };
  const auto& data() const {
    return static_cast<Storage*>(this->storage)->data;
  }

  template <class Data>
  OutOfLineInstrT(size_t input_count, const Data& data, Zone* zone)
      : InstructionT<Derived, OutOfLineInstr>(
            static_cast<uint32_t>(input_count)) {
    size_t inputs_size = input_count;
    if (alignof(Storage) > alignof(InstrIndex)) {
      inputs_size = RoundUp(input_count * sizeof(InstrIndex), alignof(Storage));
    }
    void* ptr = static_cast<char*>(
                    zone->Allocate<Storage>(inputs_size + sizeof(Storage))) +
                inputs_size;
    this->storage = new (ptr) Storage{data};
  }

  template <class Data>
  OutOfLineInstrT(base::Vector<const InstrIndex> inputs, const Data& data,
                  Zone* zone)
      : OutOfLineInstrT(inputs.size(), data, zone) {
    this->Inputs().OverwriteWith(inputs);
  }

  OutOfLineInstrT(base::Vector<const InstrIndex> inputs, Zone* zone)
      : OutOfLineInstrT(inputs, typename Derived::Data{}, zone) {}
};

struct AddInstr : InstructionT<AddInstr> {
  MachineRepresentation rep;
  InstrIndex inputs[2];

  static const bool is_block_terminator = false;
  static const bool is_writing = false;
  static const bool is_reading = false;

  InstrIndex left() const { return inputs[0]; }
  InstrIndex right() const { return inputs[1]; }

  AddInstr(InstrIndex left, InstrIndex right, MachineRepresentation rep)
      : rep(rep), inputs{left, right} {}
};

struct SubInstr : InstructionT<SubInstr> {
  MachineRepresentation rep;
  InstrIndex inputs[2];

  static const bool is_block_terminator = false;
  static const bool is_writing = false;
  static const bool is_reading = false;

  InstrIndex left() const { return inputs[0]; }
  InstrIndex right() const { return inputs[1]; }

  SubInstr(InstrIndex left, InstrIndex right, MachineRepresentation rep)
      : rep(rep), inputs{left, right} {}
};

struct PhiInstr : OutOfLineInstrT<PhiInstr> {
  struct Data {};
  using OutOfLineInstrT<PhiInstr>::OutOfLineInstrT;
  static const bool is_block_terminator = false;
  static const bool is_writing = false;
  static const bool is_reading = false;
};

struct BinaryPhiInstr : InstructionT<BinaryPhiInstr> {
  InstrIndex inputs[2];

  static const bool is_block_terminator = false;
  static const bool is_writing = false;
  static const bool is_reading = false;

  InstrIndex first() const { return inputs[0]; }
  InstrIndex second() const { return inputs[1]; }

  BinaryPhiInstr(InstrIndex first, InstrIndex second) : inputs{first, second} {}
};

// Only used while VarAssembler is running and the loop backedge has not been
// emitted yet.
struct PendingVariableLoopPhiInstr : InstructionT<PendingVariableLoopPhiInstr> {
  InstrIndex inputs[1];
  Variable* variable;

  static const bool is_block_terminator = false;
  static const bool is_writing = false;
  static const bool is_reading = false;

  InstrIndex first() const { return inputs[0]; }

  PendingVariableLoopPhiInstr(InstrIndex first, Variable* variable)
      : inputs{first}, variable(variable) {}
};

// Only used when moving a loop phi to a new graph while the loop backedge has
// not been emitted yet.
struct PendingLoopPhiInstr : InstructionT<PendingLoopPhiInstr> {
  InstrIndex inputs[1];
  union {
    // Used when transforming a TurboShaft graph.
    // This is not an input because it refers to the old graph.
    InstrIndex old_backedge_index;
    // Used when translating from sea-of-nodes.
    Node* old_backedge_node;
  };

  static const bool is_block_terminator = false;
  static const bool is_writing = false;
  static const bool is_reading = false;

  InstrIndex first() const { return inputs[0]; }

  PendingLoopPhiInstr(InstrIndex first, InstrIndex old_backedge_index)
      : inputs{first}, old_backedge_index(old_backedge_index) {
    DCHECK_NE(old_backedge_index, InstrIndex::kInvalid);
  }
  PendingLoopPhiInstr(InstrIndex first, Node* old_backedge_node)
      : inputs{first}, old_backedge_node(old_backedge_node) {}
};

struct LoopPhiInstr : InstructionT<LoopPhiInstr> {
  InstrIndex inputs[2];
  BlockIndex backedge_block;

  static const bool is_block_terminator = false;
  static const bool is_writing = false;
  static const bool is_reading = false;

  InstrIndex first() const { return inputs[0]; }
  InstrIndex second() const { return inputs[1]; }

  LoopPhiInstr(InstrIndex first, InstrIndex second, BlockIndex backedge_block)
      : inputs{first, second}, backedge_block(backedge_block) {
    DCHECK_NE(backedge_block, BlockIndex::kInvalid);
  }
};

struct ConstantInstr : InstructionT<ConstantInstr> {
  enum class Kind : uint8_t {
    kWord32,
    kWord64,
    kExternal,
    kHeapObject,
    kCompressedHeapObject,
    kSmi
  };

  Kind kind;
  union {
    uint64_t integral;
    ExternalReference external;
    Handle<HeapObject> handle;
  };

  static const bool is_block_terminator = false;
  static const bool is_writing = false;
  static const bool is_reading = false;

  static ConstantInstr Word32(uint32_t constant) {
    return ConstantInstr(Kind::kWord32, constant);
  }
  static ConstantInstr Word64(uint64_t constant) {
    return ConstantInstr(Kind::kWord64, constant);
  }
  static ConstantInstr External(ExternalReference constant) {
    return ConstantInstr(constant);
  }
  static ConstantInstr HeapObject(Handle<i::HeapObject> constant) {
    return ConstantInstr(Kind::kHeapObject, constant);
  }
  static ConstantInstr CompressedHeapObject(Handle<i::HeapObject> constant) {
    return ConstantInstr(Kind::kCompressedHeapObject, constant);
  }

 private:
  ConstantInstr(Kind kind, uint64_t integral) : kind(kind) {
    this->integral = integral;
  }
  explicit ConstantInstr(ExternalReference constant) : kind(Kind::kExternal) {
    this->external = constant;
  }
  explicit ConstantInstr(Kind kind, Handle<i::HeapObject> constant)
      : kind(kind) {
    this->handle = constant;
  }
};

struct LoadInstr : InstructionT<LoadInstr> {
  enum class Kind : uint8_t { kOnHeap, kRaw };
  MachineType loaded_rep;
  InstrIndex inputs[1];
  int32_t offset;
  Kind kind;

  static const bool is_block_terminator = false;
  static const bool is_writing = false;
  static const bool is_reading = true;

  InstrIndex base() const { return inputs[0]; }

  // Used for untagged pointers.
  static LoadInstr Raw(MachineType loaded_rep, InstrIndex base,
                       int32_t offset = 0) {
    return LoadInstr(Kind::kRaw, loaded_rep, base, offset);
  }
  // Used for tagged base pointer. The tag will be subtracted automatically.
  static LoadInstr OnHeap(MachineType loaded_rep, InstrIndex base,
                          int32_t offset = 0) {
    return LoadInstr(Kind::kOnHeap, loaded_rep, base, offset);
  }

  LoadInstr(Kind kind, MachineType loaded_rep, InstrIndex base, int32_t offset)
      : loaded_rep(loaded_rep), inputs{base}, offset(offset), kind(kind) {}
};

struct StackPointerGreaterThanInstr
    : InstructionT<StackPointerGreaterThanInstr> {
  StackCheckKind kind;
  InstrIndex inputs[1];

  static const bool is_block_terminator = false;
  static const bool is_writing = false;
  static const bool is_reading = true;

  InstrIndex stack_limit() const { return inputs[0]; }

  StackPointerGreaterThanInstr(StackCheckKind kind, InstrIndex stack_limit)
      : kind(kind), inputs{stack_limit} {}
};

struct LoadStackCheckOffsetInstr : InstructionT<LoadStackCheckOffsetInstr> {
  static const bool is_block_terminator = false;
  static const bool is_writing = false;
  static const bool is_reading = false;
};

// A Checkpoint instruction describes a potential deopt point.
// Multiple check instructions can go back to the same checkpoint.
// Checkpoints can depend on a preceding and dominating checkpoint to enable a
// more compact encoding of deopt information.
struct CheckpointInstr : OutOfLineInstrT<CheckpointInstr> {
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

  static const bool is_block_terminator = false;
  static const bool is_writing = false;
  static const bool is_reading = true;

  static CheckpointInstr Full(base::Vector<const InstrIndex> inputs,
                              const FrameStateInfo& frame_state_info,
                              Zone* zone) {
    CheckpointInstr result = CheckpointInstr(
        inputs.size(), Data{Kind::kFull, frame_state_info}, zone);
    result.Inputs().OverwriteWith(inputs);
    return result;
  }

  static CheckpointInstr Differential(InstrIndex base_checkpoint,
                                      base::Vector<const InstrIndex> inputs,
                                      const FrameStateInfo& frame_state_info,
                                      Zone* zone) {
    CheckpointInstr result = CheckpointInstr(
        inputs.size() + 1, Data{Kind::kDifferential, frame_state_info}, zone);
    base::Vector<InstrIndex> v = result.Inputs();
    v[0] = base_checkpoint;
    v.SubVector(1, v.size()).OverwriteWith(inputs);
    return result;
  }

 private:
  CheckpointInstr(size_t input_count, Data data, Zone* zone)
      : OutOfLineInstrT<CheckpointInstr>(input_count, data, zone) {}
};

// CheckLazyDeoptInstr should always immediately follow a call and a checkpoint.
// Semantically, it deopts to the checkpoint if the current code object has been
// deoptimized. But this might also be implemented differently.
struct CheckLazyDeoptInstr : InstructionT<CheckLazyDeoptInstr> {
  InstrIndex inputs[1];

  static const bool is_block_terminator = false;
  // TODO(tebbi): Distinguish between control flow and memory side effects.
  static const bool is_writing = true;
  static const bool is_reading = true;

  InstrIndex checkpoint() const { return inputs[0]; }

  explicit CheckLazyDeoptInstr(InstrIndex checkpoint) : inputs{checkpoint} {}
};

struct ParameterInstr : InstructionT<ParameterInstr> {
  uint32_t parameter_index;
  const char* debug_name;

  static const bool is_block_terminator = false;
  static const bool is_writing = true;
  static const bool is_reading = true;

  explicit ParameterInstr(uint32_t parameter_index, const char* debug_name = "")
      : parameter_index(parameter_index), debug_name(debug_name) {}
};

struct CallInstr : OutOfLineInstrT<CallInstr> {
  struct Data {
    const CallDescriptor* descriptor;
  };

  static const bool is_block_terminator = false;
  static const bool is_writing = true;
  static const bool is_reading = true;

  InstrIndex code() const { return Inputs()[0]; }
  base::Vector<const InstrIndex> arguments() const {
    return Inputs().SubVector(1, input_count);
  }

  CallInstr(const CallDescriptor* descriptor, InstrIndex code,
            base::Vector<const InstrIndex> arguments, Zone* zone)
      : OutOfLineInstrT<CallInstr>(1 + arguments.size(), Data{descriptor},
                                   zone) {
    base::Vector<InstrIndex> inputs = Inputs();
    inputs[0] = code;
    inputs.SubVector(1, inputs.size()).OverwriteWith(arguments);
  }
};

struct ReturnInstr : InstructionT<ReturnInstr> {
  InstrIndex inputs[1];

  static const bool is_block_terminator = true;
  static const bool is_writing = false;
  static const bool is_reading = false;

  InstrIndex return_value() const { return inputs[0]; }

  explicit ReturnInstr(InstrIndex return_value) : inputs{return_value} {}
};

struct GotoInstr : InstructionT<GotoInstr> {
  Block* destination;

  static const bool is_block_terminator = true;
  static const bool is_writing = false;
  static const bool is_reading = false;

  explicit GotoInstr(Block* destination) : destination(destination) {}
};

struct BranchInstr : OutOfLineInstrT<BranchInstr> {
  struct Data {
    Block* if_true;
    Block* if_false;
  };

  static const bool is_block_terminator = true;
  static const bool is_writing = false;
  static const bool is_reading = false;

  InstrIndex condition() const { return Inputs()[0]; }
  Block* if_true() const { return data().if_true; }
  Block* if_false() const { return data().if_false; }

  explicit BranchInstr(InstrIndex condition, Block* if_true, Block* if_false,
                       Zone* zone)
      : OutOfLineInstrT<BranchInstr>(base::VectorOf({condition}),
                                     Data{if_true, if_false}, zone) {}
};

#define INSTRUCTION_IS_WRITING_CASE(Name) Name##Instr::IsWriting(),
static constexpr bool kInstructionIsWritingTable[kNumberOfOpcodes] = {
    INSTRUCTION_LIST(INSTRUCTION_IS_WRITING_CASE)};
#undef INSTRUCTION_IS_WRITING_CASE

#define INSTRUCTION_IS_READING_CASE(Name) Name##Instr::IsReading(),
static constexpr bool kInstructionIsReadingTable[kNumberOfOpcodes] = {
    INSTRUCTION_LIST(INSTRUCTION_IS_READING_CASE)};
#undef INSTRUCTION_IS_READING_CASE

#define INSTRUCTION_IS_REQUIRED_WHEN_UNUSED_CASE(Name) \
  Name##Instr::IsRequiredWhenUnused(),
static constexpr bool kInstructionIsRequiredWhenUnusedTable[kNumberOfOpcodes] =
    {INSTRUCTION_LIST(INSTRUCTION_IS_REQUIRED_WHEN_UNUSED_CASE)};
#undef INSTRUCTION_IS_REQUIRED_WHEN_UNUSED_CASE

#define INSTRUCTION_OPCODE_MAP_CASE(Name)    \
  template <>                                \
  struct Instruction::opcode_of<Name##Instr> \
      : std::integral_constant<Opcode, Opcode::k##Name> {};
INSTRUCTION_LIST(INSTRUCTION_OPCODE_MAP_CASE)
#undef INSTRUCTION_OPCODE_MAP_CASE

template <class Instr, class = void>
struct static_instruction_input_count : std::integral_constant<uint32_t, 0> {};
template <class Instr>
struct static_instruction_input_count<Instr,
                                      base::void_t<decltype(Instr::inputs)>>
    : std::integral_constant<uint32_t,
                             sizeof(Instr::inputs) / sizeof(InstrIndex)> {
  STATIC_ASSERT(offsetof(Instr, inputs) == Instruction::kInputsOffset);
};
constexpr uint32_t kInstructionInputCountTable[kNumberOfInlineOpcodes] = {
#define INPUTS_OFFSET(Name) static_instruction_input_count<Name##Instr>::value,
    INLINE_INSTRUCTION_LIST(INPUTS_OFFSET)
#undef INPUTS_OFFSET
};

template <class Instr, class = void>
struct static_instruction_storage_size : std::integral_constant<size_t, 0> {};
template <class Instr>
struct static_instruction_storage_size<Instr,
                                       base::void_t<decltype(Instr::Storage)>>
    : std::integral_constant<size_t, sizeof(Instr::Storage)> {};
constexpr size_t kInstructionStorageSizeTable[kNumberOfInlineOpcodes] = {
#define INPUTS_OFFSET(Name) static_instruction_storage_size<Name##Instr>::value,
    INLINE_INSTRUCTION_LIST(INPUTS_OFFSET)
#undef INPUTS_OFFSET
};

inline size_t Instruction::InputCount() const {
  Opcode opcode = this->opcode;
  if (opcode < Opcode::kNumberOfInlineOpcodes) {
    return kInstructionInputCountTable[ToUnderlyingType(opcode)];
  } else {
    return static_cast<const OutOfLineInstr*>(this)->InputCount();
  }
}

template <class Derived, class Base>
size_t InstructionT<Derived, Base>::InputCount() const {
  if (opcode() < Opcode::kNumberOfInlineOpcodes) {
    return kInstructionInputCountTable[ToUnderlyingType(opcode())];
  } else {
    return Base::InputCount();
  }
}

inline base::Vector<const InstrIndex> Instruction::Inputs() const {
  Opcode opcode = this->opcode;
  if (opcode < Opcode::kNumberOfInlineOpcodes) {
    const InstrIndex* first = reinterpret_cast<const InstrIndex*>(
        reinterpret_cast<const char*>(this) + Instruction::kInputsOffset);
    size_t count = kInstructionInputCountTable[ToUnderlyingType(opcode)];
    return {first, count};
  } else {
    return static_cast<const OutOfLineInstr*>(this)->Inputs();
  }
}

inline base::Vector<InstrIndex> Instruction::Inputs() {
  Opcode opcode = this->opcode;
  if (opcode < Opcode::kNumberOfInlineOpcodes) {
    InstrIndex* first = reinterpret_cast<InstrIndex*>(
        reinterpret_cast<char*>(this) + Instruction::kInputsOffset);
    size_t count = kInstructionInputCountTable[ToUnderlyingType(opcode)];
    return {first, count};
  } else {
    return static_cast<OutOfLineInstr*>(this)->Inputs();
  }
}

inline InstructionStorage Instruction::ReplaceInputs(
    base::Vector<const InstrIndex> inputs, Zone* zone) const {
  InstructionStorage result;
  memcpy(&result, this, kInstructionSize);
  if (opcode < Opcode::kNumberOfInlineOpcodes) {
    DCHECK_EQ(inputs.size(),
              kInstructionInputCountTable[ToUnderlyingType(opcode)]);
    Instruction& instr = *reinterpret_cast<Instruction*>(&result);
    std::copy(inputs.begin(), inputs.end(), instr.Inputs().begin());
  } else {
    const OutOfLineInstr& self = *static_cast<const OutOfLineInstr*>(this);
    OutOfLineInstr& instr = *reinterpret_cast<OutOfLineInstr*>(&result);

    size_t inputs_size =
        RoundUp(inputs.size() * sizeof(InstrIndex), alignof(void*));
    size_t storage_size =
        kInstructionStorageSizeTable[ToUnderlyingType(opcode)];
    OutOfLineInstr::Storage* new_storage =
        reinterpret_cast<OutOfLineInstr::Storage*>(
            static_cast<char*>(zone->Allocate<OutOfLineInstr::Storage>(
                inputs_size + storage_size)) +
            inputs_size);
    memcpy(new_storage, self.storage, storage_size);
    instr.input_count = static_cast<uint32_t>(inputs.size());
    memcpy(reinterpret_cast<char*>(new_storage) -
               inputs.size() * sizeof(InstrIndex),
           inputs.data(), inputs.size() * sizeof(InstrIndex));
    instr.storage = new_storage;
  }
  return result;
}

inline bool Instruction::IsReading() const {
  return kInstructionIsReadingTable[ToUnderlyingType(opcode)];
}

inline bool Instruction::IsWriting() const {
  return kInstructionIsWritingTable[ToUnderlyingType(opcode)];
}

inline bool Instruction::IsRequiredWhenUnused() const {
  return kInstructionIsRequiredWhenUnusedTable[ToUnderlyingType(opcode)];
}

}  // namespace turboshaft
}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_TURBOSHAFT_INSTRUCTIONS_H_
