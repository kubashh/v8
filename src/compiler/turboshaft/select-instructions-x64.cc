// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Include the platform-indepenent part of instruction selection, which is not
// compiled separately. Having all of instruction selection in one compilation
// unit is important for performance.
#include "src/base/logging.h"
#include "src/base/optional.h"
#include "src/base/small-vector.h"
#include "src/base/template-utils.h"
#include "src/codegen/machine-type.h"
#include "src/codegen/turbo-assembler.h"
#include "src/compiler/backend/instruction-codes.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/compiler/turboshaft/select-instructions-inl.h"

namespace v8 {
namespace internal {
namespace compiler {
namespace turboshaft {

namespace {

struct PlatformSpecificInstructionSelector : InstructionSelector {
  using InstructionSelector::InstructionSelector;

  using InstructionSelector::CanBeImmediate;
  bool CanBeImmediate(int32_t value) {
    // int32_t min will overflow if displacement mode is
    // kNegativeDisplacement.
    return value != std::numeric_limits<int32_t>::min();
  }

  bool CanBeMemoryOperand(InstructionCode opcode, const Operation& input) {
    OpIndex input_idx = Index(input);
    if (!input.Is<LoadOp>() || !CanCover(input_idx)) {
      return false;
    }
    const LoadOp& load = input.Cast<LoadOp>();
    if (current_effect_level != effect_level[input_idx]) {
      return false;
    }
    MachineRepresentation rep = load.loaded_rep.representation();
    switch (opcode) {
      case kX64And:
      case kX64Or:
      case kX64Xor:
      case kX64Add:
      case kX64Sub:
      case kX64Push:
      case kX64Cmp:
      case kX64Test:
        // When pointer compression is enabled 64-bit memory operands can't be
        // used for tagged values.
        return rep == MachineRepresentation::kWord64 ||
               (!COMPRESS_POINTERS_BOOL && IsAnyTagged(rep));
      case kX64And32:
      case kX64Or32:
      case kX64Xor32:
      case kX64Add32:
      case kX64Sub32:
      case kX64Cmp32:
      case kX64Test32:
        // When pointer compression is enabled 32-bit memory operands can be
        // used for tagged values.
        return rep == MachineRepresentation::kWord32 ||
               (COMPRESS_POINTERS_BOOL &&
                (IsAnyTagged(rep) || IsAnyCompressed(rep)));
      case kAVXFloat64Add:
      case kAVXFloat64Sub:
      case kAVXFloat64Mul:
        DCHECK_EQ(MachineRepresentation::kFloat64, rep);
        return true;
      case kAVXFloat32Add:
      case kAVXFloat32Sub:
      case kAVXFloat32Mul:
        DCHECK_EQ(MachineRepresentation::kFloat32, rep);
        return true;
      case kX64Cmp16:
      case kX64Test16:
        return rep == MachineRepresentation::kWord16;
      case kX64Cmp8:
      case kX64Test8:
        return rep == MachineRepresentation::kWord8;
      default:
        break;
    }
    return false;
  }

  AddressingMode GenerateMemoryOperandInputs(
      const Operation& op, base::SmallVector<InstructionOperand, 8>* inputs) {
    const LoadOp& load = op.Cast<LoadOp>();
    if (auto* object = Get(load.base()).TryCast<ConstantOp>()) {
      if (object->kind == ConstantOp::Kind::kExternal &&
          CanAddressRelativeToRootsRegister(object->external_reference())) {
        ptrdiff_t delta =
            ptrdiff_t{load.offset} +
            TurboAssemblerBase::RootRegisterOffsetForExternalReference(
                isolate(), object->external_reference());
        if (is_int32(delta)) {
          inputs->emplace_back(UseImmediate(static_cast<int32_t>(delta)));
          return kMode_Root;
        }
      }
    }
    if (!CanBeImmediate(load.offset)) {
      // This is a very rare case. Create an ad-hoc constant.
      int virtual_register = sequence->NextVirtualRegister();
      sequence->AddConstant(virtual_register, Constant(load.offset));
      Emit(kArchNop, {ConstantOperand(virtual_register)}, {});
      inputs->push_back(UseRegister(load.base()));
      inputs->push_back(UnallocatedOperand(
          UnallocatedOperand::MUST_HAVE_REGISTER,
          UnallocatedOperand::USED_AT_START, virtual_register));
      return kMode_MR1;
    }
    // TODO(tebbi): Match index and scale.
    return GenerateMemoryOperandInputs(load.base(), base::nullopt, 0,
                                       load.offset, inputs);
  }

  AddressingMode GenerateMemoryOperandInputs(
      OpIndex base, base::Optional<OpIndex> index, int scale_exponent,
      int32_t displacement, base::SmallVector<InstructionOperand, 8>* inputs,
      RegisterUseKind reg_kind = RegisterUseKind::kUseRegister) {
    DCHECK(CanBeImmediate(displacement));
    AddressingMode mode = kMode_MRI;
    inputs->push_back(UseRegister(base, reg_kind));
    if (index.has_value()) {
      DCHECK(scale_exponent >= 0 && scale_exponent <= 3);
      inputs->push_back(UseRegister(*index, reg_kind));
      if (displacement != 0) {
        inputs->push_back(UseImmediate(displacement));
        static const AddressingMode kMRnI_modes[] = {kMode_MR1I, kMode_MR2I,
                                                     kMode_MR4I, kMode_MR8I};
        mode = kMRnI_modes[scale_exponent];
      } else {
        static const AddressingMode kMRn_modes[] = {kMode_MR1, kMode_MR2,
                                                    kMode_MR4, kMode_MR8};
        mode = kMRn_modes[scale_exponent];
      }
    } else {
      if (displacement == 0) {
        mode = kMode_MR;
      } else {
        inputs->push_back(UseImmediate(displacement));
        mode = kMode_MRI;
      }
    }
    return mode;
  }
};

bool InstructionSelector::CanBeImmediate(const Operation& value) {
  if (!value.Is<ConstantOp>()) return false;
  const ConstantOp& constant = value.Cast<ConstantOp>();
  switch (constant.kind) {
    using Kind = ConstantOp::Kind;
    case Kind::kWord32:
      return specific().CanBeImmediate(static_cast<int32_t>(constant.word32()));
    case Kind::kWord64: {
      const int64_t value = static_cast<int64_t>(constant.word64());
      // int32_t min will overflow if displacement mode is
      // kNegativeDisplacement.
      return std::numeric_limits<int32_t>::min() < value &&
             value <= std::numeric_limits<int32_t>::max();
    }
    default:
      return false;
  }
}

void InstructionSelector::VisitWordNotEqualZero(const Operation& value,
                                                const FlagsContinuation& cont) {
  switch (value.opcode) {
    case Opcode::kEqual: {
      auto& cmp = value.Cast<EqualOp>();
      OpIndex left = cmp.left();
      OpIndex right = cmp.right();
      if (cmp.rep == MachineRepresentation::kWord32) {
        InstructionCode opcode =
            kX64Cmp32 | FlagsModeField::encode(FlagsMode::kFlags_branch) |
            FlagsConditionField::encode(FlagsCondition::kEqual);
        EmitWithContinuation(opcode, cont, {}, {UseRegister(left), Use(right)});
      } else {
        UNIMPLEMENTED();
      }
      break;
    }
    case Opcode::kStackPointerGreaterThan: {
      VisitStackPointerGreaterThan(value.Cast<StackPointerGreaterThanOp>(),
                                   cont);
      break;
    }
    default: {
      InstructionCode opcode =
          kX64Cmp32 | FlagsModeField::encode(FlagsMode::kFlags_branch) |
          FlagsConditionField::encode(FlagsCondition::kNotEqual);
      EmitWithContinuation(opcode, cont, {},
                           {UseRegister(Index(value)), UseImmediate(0)});
      break;
    }
  }
}

void InstructionSelector::VisitBinop(OpIndex op, Binop op_kind,
                                     MachineRepresentation rep, OpIndex left,
                                     OpIndex right) {
  InstructionCode opcode;
  switch (op_kind) {
    case Binop::kBitwiseAnd:
      switch (rep) {
        case MachineRepresentation::kWord32:
          opcode = kX64And32;
          break;
        default:
          UNIMPLEMENTED();
      }
      break;
    case Binop::kAdd:
      switch (rep) {
        case MachineRepresentation::kWord32:
          opcode = kX64Add32;
          break;
        default:
          UNIMPLEMENTED();
      }
      break;
    case Binop::kSub:
      switch (rep) {
        case MachineRepresentation::kWord32:
          opcode = kX64Sub32;
          break;
        default:
          UNIMPLEMENTED();
      }
      break;
  }

  base::SmallVector<InstructionOperand, 8> inputs;

  if (left == right) {
    // If both inputs refer to the same operand, enforce allocating a register
    // for both of them to ensure that we don't end up generating code like
    // this:
    //
    //   mov rax, [rbp-0x10]
    //   add rax, [rbp-0x10]
    //   jo label
    InstructionOperand const input = UseRegister(left);
    inputs = base::make_array(input, input);
  } else if (specific().CanBeImmediate(Get(right))) {
    inputs = base::make_array(UseRegister(left),
                              UseImmediate(Get(right).Cast<ConstantOp>()));
  } else {
    if (IsCommutative(op_kind) && !IsLive(right) &&
        (IsLive(left) || !specific().CanBeMemoryOperand(opcode, Get(right)))) {
      std::swap(left, right);
    }
    const Operation& right_op = Get(right);
    if (specific().CanBeMemoryOperand(opcode, right_op)) {
      inputs.push_back(UseRegister(left));
      AddressingMode addressing_mode =
          specific().GenerateMemoryOperandInputs(right_op, &inputs);
      opcode |= AddressingModeField::encode(addressing_mode);
    } else {
      inputs = base::make_array(UseRegister(left), Use(right));
    }
  }
  auto outputs = base::VectorOf({DefineSameAsInput(op, 0)});
  Emit(opcode, outputs, base::VectorOf(inputs));
}

void InstructionSelector::EmitPrepareArguments(
    base::Vector<const OpIndex> arguments,
    const CallDescriptor* call_descriptor) {
  // Prepare for C function call.
  if (call_descriptor->IsCFunctionCall()) {
    Emit(kArchPrepareCallCFunction | MiscField::encode(static_cast<int>(
                                         call_descriptor->ParameterCount())),
         {}, {});

    // Poke any stack arguments.
    for (size_t n = 0; n < arguments.size(); ++n) {
      OpIndex input_idx = arguments[n];
      const Operation& input = Get(input_idx);

      int slot = static_cast<int>(n);
      InstructionOperand value = CanBeImmediate(input)
                                     ? UseImmediate(input.Cast<ConstantOp>())
                                     : UseRegister(input_idx);
      Emit(kX64Poke | MiscField::encode(slot), {}, {value});
    }
  } else {
    // Push any stack arguments.
    int stack_decrement = 0;
    for (OpIndex argument_idx : base::Reversed(arguments)) {
      const Operation& argument = Get(argument_idx);
      stack_decrement += kSystemPointerSize;
      InstructionOperand decrement = UseImmediate(stack_decrement);
      stack_decrement = 0;
      if (CanBeImmediate(argument)) {
        Emit(kX64Push, {},
             {decrement, UseImmediate(argument.Cast<ConstantOp>())});
      } else if ((cpu_features & INTEL_ATOM) ||
                 sequence->IsFP(GetVirtualRegister(argument_idx))) {
        // TODO(titzer): X64Push cannot handle stack->stack double moves
        // because there is no way to encode fixed double slots.
        Emit(kX64Push, {}, {decrement, UseRegister(argument_idx)});
      } else if (specific().CanBeMemoryOperand(kX64Push, argument)) {
        base::SmallVector<InstructionOperand, 8> inputs;
        size_t input_count = 0;
        inputs[input_count++] = decrement;
        AddressingMode mode =
            specific().GenerateMemoryOperandInputs(argument, &inputs);
        InstructionCode opcode = kX64Push | AddressingModeField::encode(mode);
        Emit(opcode, {}, base::VectorOf(inputs));
      } else {
        Emit(kX64Push, {}, {decrement, UseRegisterOrSlot(argument_idx)});
      }
    }
  }
}

void InstructionSelector::EmitPrepareResults(
    base::Vector<const OpIndex> results,
    const CallDescriptor* call_descriptor) {
  for (size_t i = 0; i < results.size(); ++i) {
    OpIndex result = results[i];
    LinkageLocation location = call_descriptor->GetReturnLocation(i);
    if (!location.IsCallerFrameSlot()) continue;
    // TODO(tebbi): Skip alignment holes in nodes.
    DCHECK(!call_descriptor->IsCFunctionCall());
    MarkAsRepresentation(location.GetType().representation(), result);
    int offset = call_descriptor->GetOffsetToReturns();
    int reverse_slot = -location.GetLocation() - offset;
    Emit(kX64Peek, {DefineAsRegister(result)}, {UseImmediate(reverse_slot)});
  }
}

void InstructionSelector::VisitStackPointerGreaterThan(
    const StackPointerGreaterThanOp& op, const FlagsContinuation& cont) {
  InstructionCode opcode =
      kArchStackPointerGreaterThan |
      MiscField::encode(static_cast<int>(op.kind)) |
      FlagsConditionField::encode(kStackPointerGreaterThanCondition);

  const Operation& stack_limit = Get(op.stack_limit());
  if (specific().CanBeMemoryOperand(kX64Cmp, stack_limit)) {
    base::SmallVector<InstructionOperand, 8> inputs;
    AddressingMode addressing_mode =
        specific().GenerateMemoryOperandInputs(stack_limit, &inputs);
    opcode |= AddressingModeField::encode(addressing_mode);

    EmitWithContinuation(opcode, cont, {}, base::VectorOf(inputs));
  } else {
    EmitWithContinuation(opcode, cont, {}, {UseRegister(Index(stack_limit))});
  }
}

}  // namespace

bool SelectInstructions(
    Zone* phase_zone, Linkage* linkage, InstructionSequence* sequence,
    const turboshaft::Graph& graph, SourcePositionTable* source_positions,
    Frame* frame, bool enable_switch_jump_table, TickCounter* tick_counter,
    JSHeapBroker* broker, size_t* max_unoptimized_frame_height,
    size_t* max_pushed_argument_count, bool collect_all_source_positons,
    base::Flags<CpuFeature> cpu_features, bool enable_instruction_scheduling,
    bool enable_roots_relative_addressing, bool trace_turbo) {
  PlatformSpecificInstructionSelector selector{phase_zone,
                                               graph,
                                               sequence,
                                               frame,
                                               linkage,
                                               cpu_features,
                                               tick_counter,
                                               enable_instruction_scheduling,
                                               enable_roots_relative_addressing,
                                               max_pushed_argument_count};
  return selector.Run();
}

}  // namespace turboshaft
}  // namespace compiler
}  // namespace internal
}  // namespace v8
