// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/baseline/baseline-compiler.h"

#include "src/baseline-compilation-info.h"
#include "src/baseline/jump-target-analysis.h"
#include "src/code-stub-assembler.h"
#include "src/compiler.h"
#include "src/handler-table.h"
#include "src/handles-inl.h"
#include "src/interpreter/bytecode-array-iterator.h"
#include "src/interpreter/bytecode-flags.h"
#include "src/interpreter/interpreter.h"

namespace v8 {
namespace internal {
namespace baseline {

using interpreter::Register;
using compiler::Node;

class SparkplugCompilationJob final : public BaselineCompilationJob {
 public:
  // TODO(rmcilroy): Remove isolate from arguments.
  SparkplugCompilationJob(uintptr_t stack_limit, Isolate* isolate,
                          BaselineCompilationInfo* info)
      : BaselineCompilationJob(stack_limit, info),
        state_(isolate, compilation_info()->zone(),
               compilation_info()->num_parameters_including_this(),
               Code::BASELINE_FUNCTION,
               compilation_info()->GetDebugName().get(),
               // TODO(rmcilroy): Implement appropriate poisoning.
               PoisoningMitigationLevel::kDontPoison),
        compiler_(
            &state_, handle(compilation_info()->bytecode_array()),
            compilation_info()->feedback_vector(),
            compilation_info()->shared_info()->has_duplicate_parameters()) {}

 protected:
  Status ExecuteJobImpl() final;
  Status FinalizeJobImpl(Isolate* isolate) final;

 private:
  compiler::CodeAssemblerState state_;
  BaselineCompiler compiler_;

  DISALLOW_COPY_AND_ASSIGN(SparkplugCompilationJob);
};

CompilationJob::Status SparkplugCompilationJob::ExecuteJobImpl() {
  return SUCCEEDED;
}

CompilationJob::Status SparkplugCompilationJob::FinalizeJobImpl(
    Isolate* isolate) {
  // TODO(rmcilroy): Move compilation to ExecuteJob.
  if (!compiler_.Compile()) {
    return FAILED;
  }
  Handle<Code> code = compiler::CodeAssembler::GenerateCode(&state_);

#ifdef ENABLE_DISASSEMBLER
  if (FLAG_print_baseline_code) {
    OFStream os(stdout);
    code->Disassemble(compilation_info()->GetDebugName().get(), os);
    os << std::flush;
  }
#endif  // ENABLE_DISASSEMBLER

  compilation_info()->SetCode(code);
  return SUCCEEDED;
}

BaselineCompilationJob* BaselineCompiler::NewCompilationJob(
    uintptr_t stack_limit, Isolate* isolate, BaselineCompilationInfo* info) {
  return new SparkplugCompilationJob(stack_limit, isolate, info);
}

BaselineCompiler::BaselineCompiler(compiler::CodeAssemblerState* state,
                                   Handle<BytecodeArray> bytecode_array,
                                   Handle<FeedbackVector> feedback_vector,
                                   bool has_duplicate_parameters)
    : CodeStubAssembler(state),
      isolate_(bytecode_array->GetIsolate()),
      bytecode_array_(bytecode_array),
      register_count_(bytecode_array_->register_count()),
      parameter_count_(bytecode_array_->parameter_count()),
      has_duplicate_parameters_(has_duplicate_parameters),
      VARIABLE_CONSTRUCTOR(accumulator_, MachineRepresentation::kTagged),
      feedback_vector_(feedback_vector),
      exception_handlers_(zone()),
      current_exception_handler_(0),
      disable_stack_check_across_call_(false),
      stack_pointer_before_call_(nullptr),
      frame_pointer_(LoadFramePointer()),
      feedback_vector_node_(HeapConstant(feedback_vector_)),
      aborted_(false) {
  RegisterCallGenerationCallbacks(
      [this] { CallPrologue(); },
      [this](Node* result) { CallEpilogue(result); });
  Node* undefined = UndefinedConstant();
  if (FLAG_spark_locals) {
    for (int i = 0; i < register_count(); i++) {
      registers_.push_back(base::make_unique<Variable>(this, MachineRepresentation::kTagged));
      registers_[i]->Bind(undefined);
    }
  }

}

bool BaselineCompiler::Compile() {
  JumpTargetAnalysis jump_targets(zone(), bytecode_array());
  set_jump_targets(&jump_targets);

  compiler::CodeAssemblerVariableList merge_vars(zone());
  merge_vars.push_back(&accumulator_);
  if (FLAG_spark_locals) {
    for (int i = 0; i < register_count(); i++) {
      merge_vars.push_back(registers_[i].get());
    }
  }

  jump_targets.Analyse(this, merge_vars);
  JumpTargetAnalysis::Iterator jump_targets_iterator =
      jump_targets.GetIterator();

  interpreter::BytecodeArrayIterator iterator(bytecode_array());
  set_bytecode_iterator(&iterator);

  BuildStackFrame();
  IncrementInvokationCount();
  bool exit_seen_in_block = false;
  for (; !iterator.done(); iterator.Advance()) {
    if (aborted_) return false;

    if (iterator.current_offset() == jump_targets_iterator.target_offset()) {
      Label* label = jump_targets_iterator.label();
      if (!exit_seen_in_block) {
        Goto(label);
      }
      BIND(label);
      jump_targets_iterator.Next();
      exit_seen_in_block = false;
    }

    ExitThenEnterExceptionHandlers(iterator.current_offset());
    if (!exit_seen_in_block) {  // Don't generate dead code.
      switch (iterator.current_bytecode()) {
#define BYTECODE_CASE(name, ...)       \
  case interpreter::Bytecode::k##name: \
    Visit##name();                     \
    break;
        BYTECODE_LIST(BYTECODE_CASE)
#undef BYTECODE_CASE
      }
    }

    if (interpreter::Bytecodes::UnconditionallyExitsBasicBlock(
            iterator.current_bytecode())) {
      exit_seen_in_block = true;
    }
  }
  set_jump_targets(nullptr);
  set_bytecode_iterator(nullptr);

  return true;
}

void BaselineCompiler::ExitThenEnterExceptionHandlers(int current_offset) {
  HandlerTable handler_table(*bytecode_array());

  // Potentially exit exception handlers.
  while (!exception_handlers_.empty()) {
    auto top_exception = exception_handlers_.top();
    int current_end = top_exception.end_offset_;
    if (current_offset < current_end) break;  // Still covered by range.
    exception_handlers_.pop();
  }

  // Potentially enter exception handlers.
  int num_entries = handler_table.NumberOfRangeEntries();
  while (current_exception_handler_ < num_entries) {
    int next_start = handler_table.GetRangeStart(current_exception_handler_);
    if (current_offset < next_start) break;  // Not yet covered by range.

    int next_end = handler_table.GetRangeEnd(current_exception_handler_);
    int next_handler =
        handler_table.GetRangeHandler(current_exception_handler_);
    int context_register =
        handler_table.GetRangeData(current_exception_handler_);
    exception_handlers_.push(
        {next_start, next_end, next_handler, context_register});
    current_exception_handler_++;

    // Insert dummy jump to handler to keep CSA happy that it is used.
    BuildDummyHandlerJump(next_handler);
  }
}

void BaselineCompiler::BuildDummyHandlerJump(int handler_offset) {
  // Inserts a dummy jump to a handler in case nothing else jumps to it to
  // keep CSA happy. No machine code should be generated for this due to
  // constant folding.
  // TODO(rmcilroy): Ensure not code is generated.
  Label dummy(this), done(this);
  Branch(Word32Equal(Int32Constant(0), Int32Constant(1)), &dummy, &done);

  BIND(&dummy);
  accumulator_.Bind(UndefinedConstant());
  Goto(jump_targets()->LabelForTarget(handler_offset));

  BIND(&done);
}

void BaselineCompiler::AbortIfWordNotEqual(Node* lhs, Node* rhs,
                                           AbortReason abort_reason) {
  Label ok(this), abort(this, Label::kDeferred);
  Branch(WordEqual(lhs, rhs), &ok, &abort);

  BIND(&abort);
  Abort(abort_reason);

  BIND(&ok);
}

void BaselineCompiler::SaveBytecodeOffset() {
  int current_offset = bytecode_iterator()->current_offset();
  int raw_offset = current_offset + BytecodeArray::kHeaderSize - kHeapObjectTag;
  StoreRegister(Register::bytecode_offset(), SmiConstant(raw_offset));
}

void BaselineCompiler::BuildBailout() {
  if (FLAG_spark_locals) {
    for (int i = 0; i < register_count(); i++) {
      Register reg(i);
      Node* value = registers_[i]->value();
      StoreNoWriteBarrier(MachineRepresentation::kTagged,
                          frame_pointer_ /*LoadFramePointer()*/,
                          IntPtrConstant(reg.ToOperand() * kPointerSize),
                          value);
    }
  }
  CallStub(CodeFactory::BailoutFromBaselineCode(isolate()), GetContext(),
           accumulator_.value());
  Abort(AbortReason::kUnexpectedReturnFromBailout);
}

void BaselineCompiler::CallPrologue() {
  SaveBytecodeOffset();
  if (FLAG_debug_code && !disable_stack_check_across_call_) {
    DCHECK_NULL(stack_pointer_before_call_);
    stack_pointer_before_call_ = LoadStackPointer();
  }
}

void BaselineCompiler::CallEpilogue(Node* result) {
  if (FLAG_debug_code && !disable_stack_check_across_call_) {
    disable_stack_check_across_call_ = true;
    Node* stack_pointer_after_call = LoadStackPointer();
    Node* stack_pointer_before_call = stack_pointer_before_call_;
    stack_pointer_before_call_ = nullptr;
    AbortIfWordNotEqual(stack_pointer_before_call, stack_pointer_after_call,
                        AbortReason::kUnexpectedStackPointer);
    disable_stack_check_across_call_ = false;
  }

  if (!exception_handlers_.empty()) {
    int handler_offset = exception_handlers_.top().handler_offset_;
    int context_reg = exception_handlers_.top().context_register_;
    Label if_exception(this, Label::kDeferred), done(this);
    GotoIfException(result, &if_exception, &accumulator_);
    Goto(&done);

    BIND(&if_exception);
    {
      Node* context = LoadRegister(Register(context_reg));
      StoreRegister(Register::current_context(), context);
      Goto(jump_targets()->LabelForTarget(handler_offset));
    }

    BIND(&done);
  }
}

void BaselineCompiler::BuildStackFrame() {
  // This is a hack where we access the elements by StackSlot to have TF
  // allocate spill slots for them on the stackframe, but then access them later
  // via the FP pointer.

  // Stack slots seem to be allocated in reverse, so reserve registers first.
  // TODO(rmcilroy): Add some guarantees that these stack slots are layed out
  // correctly.
  Node* undefined = UndefinedConstant();
  accumulator_.Bind(undefined);
  for (int i = 0; i < register_count(); i++) {
    Node* slot = StackSlot(MachineRepresentation::kTagged);
    StoreNoWriteBarrier(MachineRepresentation::kTagged, slot, undefined);
  }

  // Now initialize bytecode offset and array.
  Node* bytecode_offset_slot = StackSlot(MachineRepresentation::kTaggedSigned);
  StoreNoWriteBarrier(MachineRepresentation::kTaggedSigned,
                      bytecode_offset_slot, SmiConstant(0));
  Node* bytecode_array_slot = StackSlot(MachineRepresentation::kTagged);
  StoreNoWriteBarrier(MachineRepresentation::kTagged, bytecode_array_slot,
                      HeapConstant(bytecode_array_));
}

void BaselineCompiler::IncrementInvokationCount() {
  Node* count = LoadObjectField(LoadFeedbackVector(),
                                FeedbackVector::kInvocationCountOffset,
                                MachineType::Int32());
  Node* new_count = Int32Add(count, Int32Constant(1));
  StoreObjectFieldNoWriteBarrier(LoadFeedbackVector(),
                                 FeedbackVector::kInvocationCountOffset,
                                 new_count, MachineRepresentation::kWord32);
}

Node* BaselineCompiler::LoadRegister(Register reg) {
  DCHECK(reg.is_valid());
  if (FLAG_spark_locals && !reg.is_parameter()) {
    return registers_[reg.index()]->value();
  }
  return Load(MachineType::AnyTagged(), frame_pointer_ /*LoadFramePointer()*/,
              IntPtrConstant(reg.ToOperand() * kPointerSize));
}

void BaselineCompiler::StoreRegister(Register reg, Node* value) {
  DCHECK(reg.is_valid());
  if (FLAG_spark_locals && !reg.is_parameter()) {
    registers_[reg.index()]->Bind(value);
  } else {
    StoreNoWriteBarrier(MachineRepresentation::kTagged,
                        frame_pointer_ /*LoadFramePointer()*/,
                        IntPtrConstant(reg.ToOperand() * kPointerSize), value);
  }
}

Node* BaselineCompiler::GetContext() {
  return LoadRegister(Register::current_context());
}

Node* BaselineCompiler::LoadFeedbackVector() {
  return feedback_vector_node_;
}

void BaselineCompiler::VisitLdaZero() {
  Comment("LdaZero");
  accumulator_.Bind(SmiConstant(0));
}

void BaselineCompiler::VisitLdaSmi() {
  Comment("LdaSmi");
  accumulator_.Bind(SmiConstant(bytecode_iterator()->GetImmediateOperand(0)));
}

void BaselineCompiler::VisitLdaConstant() {
  Comment("LdaConstant");
  Handle<Object> constant = bytecode_iterator()->GetConstantForIndexOperand(0);
  if (constant->IsSmi()) {
    accumulator_.Bind(SmiConstant(Handle<Smi>::cast(constant)->value()));
  } else {
    accumulator_.Bind(UntypedHeapConstant(Handle<HeapObject>::cast(constant)));
  }
}

void BaselineCompiler::VisitLdaUndefined() {
  Comment("LdaUndefined");
  accumulator_.Bind(UndefinedConstant());
}

void BaselineCompiler::VisitLdaNull() {
  Comment("LdaNull");
  accumulator_.Bind(NullConstant());
}

void BaselineCompiler::VisitLdaTheHole() {
  Comment("LdaTheHole");
  accumulator_.Bind(TheHoleConstant());
}

void BaselineCompiler::VisitLdaTrue() {
  Comment("LdaTrue");
  accumulator_.Bind(TrueConstant());
}

void BaselineCompiler::VisitLdaFalse() {
  Comment("LdaFalse");
  accumulator_.Bind(FalseConstant());
}

void BaselineCompiler::VisitLdar() {
  Comment("Ldar");
  accumulator_.Bind(LoadRegister(bytecode_iterator()->GetRegisterOperand(0)));
}

void BaselineCompiler::VisitStar() {
  Comment("Star");
  StoreRegister(bytecode_iterator()->GetRegisterOperand(0),
                accumulator_.value());
}

void BaselineCompiler::VisitMov() {
  Comment("Mov");
  Node* value = LoadRegister(bytecode_iterator()->GetRegisterOperand(0));
  StoreRegister(bytecode_iterator()->GetRegisterOperand(1), value);
}

void BaselineCompiler::VisitLdaGlobal() {
  Comment("LdaGlobal");
  TNode<Name> name = HeapConstant<Name>(
      Handle<Name>::cast(bytecode_iterator()->GetConstantForIndexOperand(0)));
  Node* slot = SmiConstant(bytecode_iterator()->GetIndexOperand(1));

  Node* result = CallBuiltin(Builtins::kLoadGlobalIC, GetContext(), name, slot,
                             LoadFeedbackVector());
  accumulator_.Bind(result);
}

void BaselineCompiler::VisitLdaGlobalInsideTypeof() {
  Comment("LdaGlobalInsideTypeof");
  TNode<Name> name = HeapConstant<Name>(
      Handle<Name>::cast(bytecode_iterator()->GetConstantForIndexOperand(0)));
  Node* slot = SmiConstant(bytecode_iterator()->GetIndexOperand(1));

  Node* result = CallBuiltin(Builtins::kLoadGlobalICInsideTypeof, GetContext(),
                             name, slot, LoadFeedbackVector());
  accumulator_.Bind(result);
}

void BaselineCompiler::VisitStaGlobal() {
  Comment("StaGlobal");
  TNode<Name> name = HeapConstant<Name>(
      Handle<Name>::cast(bytecode_iterator()->GetConstantForIndexOperand(0)));
  Node* slot = SmiConstant(bytecode_iterator()->GetIndexOperand(1));
  Node* value = accumulator_.value();

  CallBuiltin(Builtins::kStoreGlobalIC, GetContext(), name, value, slot,
              LoadFeedbackVector());
}

void BaselineCompiler::VisitStaInArrayLiteral() {
  Comment("StaInArrayLiteral");
  Node* array = LoadRegister(bytecode_iterator()->GetRegisterOperand(0));
  Node* index = LoadRegister(bytecode_iterator()->GetRegisterOperand(1));
  Node* slot = SmiConstant(bytecode_iterator()->GetIndexOperand(2));
  Node* value = accumulator_.value();

  Node* result = CallBuiltin(Builtins::kStoreInArrayLiteralIC, GetContext(),
                             array, index, value, slot, LoadFeedbackVector());
  accumulator_.Bind(result);
}

void BaselineCompiler::VisitStaDataPropertyInLiteral() {
  Comment("StaDataPropertyInLiteral");
  Node* object = LoadRegister(bytecode_iterator()->GetRegisterOperand(0));
  Node* name = LoadRegister(bytecode_iterator()->GetRegisterOperand(1));
  Node* flags = SmiConstant(bytecode_iterator()->GetFlagOperand(2));
  Node* slot = SmiConstant(bytecode_iterator()->GetIndexOperand(3));
  Node* value = accumulator_.value();

  CallRuntime(Runtime::kInlineDefineDataPropertyInLiteral, GetContext(), object,
              name, value, flags, LoadFeedbackVector(), slot);
}

void BaselineCompiler::VisitCollectTypeProfile() {
  Comment("CollectTypeProfile");
  Node* position = SmiConstant(bytecode_iterator()->GetImmediateOperand(0));
  Node* value = accumulator_.value();

  CallRuntime(Runtime::kInlineCollectTypeProfile, GetContext(), position, value,
              LoadFeedbackVector());
}

void BaselineCompiler::VisitLdaContextSlot() {
  Comment("LdaContextSlot");
  Node* context = LoadRegister(bytecode_iterator()->GetRegisterOperand(0));
  int slot_index = bytecode_iterator()->GetIndexOperand(1);
  size_t depth = bytecode_iterator()->GetUnsignedImmediateOperand(2);

  for (size_t i = 0; i < depth; ++i) {
    context = LoadContextElement(context, Context::PREVIOUS_INDEX);
  }

  Node* result = LoadContextElement(context, slot_index);
  accumulator_.Bind(result);
}

void BaselineCompiler::VisitLdaImmutableContextSlot() {
  Comment("LdaImmutableContextSlot");
  Node* context = LoadRegister(bytecode_iterator()->GetRegisterOperand(0));
  int slot_index = bytecode_iterator()->GetIndexOperand(1);
  size_t depth = bytecode_iterator()->GetUnsignedImmediateOperand(2);

  for (size_t i = 0; i < depth; ++i) {
    context = LoadContextElement(context, Context::PREVIOUS_INDEX);
  }

  Node* result = LoadContextElement(context, slot_index);
  accumulator_.Bind(result);
}

void BaselineCompiler::VisitLdaCurrentContextSlot() {
  Comment("LdaCurrentContextSlot");
  int slot_index = bytecode_iterator()->GetIndexOperand(0);
  Node* result = LoadContextElement(GetContext(), slot_index);
  accumulator_.Bind(result);
}

void BaselineCompiler::VisitLdaImmutableCurrentContextSlot() {
  Comment("LdaImmutableCurrentContextSlot");
  int slot_index = bytecode_iterator()->GetIndexOperand(0);
  Node* result = LoadContextElement(GetContext(), slot_index);
  accumulator_.Bind(result);
}

void BaselineCompiler::VisitStaContextSlot() {
  Comment("StaContextSlot");
  Node* context = LoadRegister(bytecode_iterator()->GetRegisterOperand(0));
  int slot_index = bytecode_iterator()->GetIndexOperand(1);
  size_t depth = bytecode_iterator()->GetUnsignedImmediateOperand(2);
  Node* value = accumulator_.value();

  for (size_t i = 0; i < depth; ++i) {
    context = LoadContextElement(context, Context::PREVIOUS_INDEX);
  }

  StoreContextElement(context, slot_index, value);
}

void BaselineCompiler::VisitStaCurrentContextSlot() {
  Comment("StaCurrentContextSlot");
  int slot_index = bytecode_iterator()->GetIndexOperand(0);
  Node* value = accumulator_.value();
  StoreContextElement(GetContext(), slot_index, value);
}

void BaselineCompiler::VisitLdaLookupSlot() {
  Comment("LdaLookupSlot");
  TNode<Name> name = HeapConstant<Name>(
      Handle<Name>::cast(bytecode_iterator()->GetConstantForIndexOperand(0)));
  Node* result =
      CallRuntime(Runtime::kInlineLoadLookupSlot, GetContext(), name);
  accumulator_.Bind(result);
}

void BaselineCompiler::VisitLdaLookupSlotInsideTypeof() {
  Comment("LdaLookupSlotInsideTypeof");
  TNode<Name> name = HeapConstant<Name>(
      Handle<Name>::cast(bytecode_iterator()->GetConstantForIndexOperand(0)));
  Node* result = CallRuntime(Runtime::kInlineLoadLookupSlotInsideTypeof,
                             GetContext(), name);
  accumulator_.Bind(result);
}

void BaselineCompiler::BuildLdaLookupContextSlot(TypeofMode typeof_mode) {
  Node* context = GetContext();
  TNode<Name> name = HeapConstant<Name>(
      Handle<Name>::cast(bytecode_iterator()->GetConstantForIndexOperand(0)));
  uint32_t slot_index = bytecode_iterator()->GetIndexOperand(1);
  Node* depth =
      Int32Constant(bytecode_iterator()->GetUnsignedImmediateOperand(2));

  Label slowpath(this, Label::kDeferred), end(this);

  // Check for context extensions to allow the fast path
  GotoIfHasContextExtensionUpToDepth(context, depth, &slowpath);

  // Fast path does a normal context load.
  Node* slot_context = GetContextAtDepth(context, depth);
  Node* result = LoadContextElement(slot_context, slot_index);
  accumulator_.Bind(result);
  Goto(&end);

  // Slow path when we have to call out to the runtime
  BIND(&slowpath);
  {
    Runtime::FunctionId function_id =
        (typeof_mode == NOT_INSIDE_TYPEOF)
            ? Runtime::kLoadLookupSlot
            : Runtime::kLoadLookupSlotInsideTypeof;
    Node* result = CallRuntime(function_id, context, name);
    accumulator_.Bind(result);
    Goto(&end);
  }
  BIND(&end);
}

void BaselineCompiler::VisitLdaLookupContextSlot() {
  Comment("LdaLookupContextSlot");
  BuildLdaLookupContextSlot(TypeofMode::NOT_INSIDE_TYPEOF);
}

void BaselineCompiler::VisitLdaLookupContextSlotInsideTypeof() {
  Comment("LdaLookupContextSlotInsideTypeof");
  BuildLdaLookupContextSlot(TypeofMode::INSIDE_TYPEOF);
}

void BaselineCompiler::BuildLdaLookupGlobalSlot(TypeofMode typeof_mode) {
  Node* context = GetContext();
  TNode<Name> name = HeapConstant<Name>(
      Handle<Name>::cast(bytecode_iterator()->GetConstantForIndexOperand(0)));
  Node* slot = SmiConstant(bytecode_iterator()->GetIndexOperand(1));
  Node* depth =
      Int32Constant(bytecode_iterator()->GetUnsignedImmediateOperand(2));

  Label slowpath(this, Label::kDeferred), end(this);

  // Check for context extensions to allow the fast path
  GotoIfHasContextExtensionUpToDepth(context, depth, &slowpath);

  // Fast path does a normal load global,
  Builtins::Name buitin_id = (typeof_mode == NOT_INSIDE_TYPEOF)
                                 ? Builtins::kLoadGlobalIC
                                 : Builtins::kLoadGlobalICInsideTypeof;
  Node* result =
      CallBuiltin(buitin_id, GetContext(), name, slot, LoadFeedbackVector());
  accumulator_.Bind(result);
  Goto(&end);

  // Slow path when we have to call out to the runtime
  BIND(&slowpath);
  {
    Runtime::FunctionId function_id =
        (typeof_mode == NOT_INSIDE_TYPEOF)
            ? Runtime::kLoadLookupSlot
            : Runtime::kLoadLookupSlotInsideTypeof;
    Node* result = CallRuntime(function_id, context, name);
    accumulator_.Bind(result);
    Goto(&end);
  }
  BIND(&end);
}

void BaselineCompiler::VisitLdaLookupGlobalSlot() {
  Comment("LdaLookupGlobalSlot");
  BuildLdaLookupGlobalSlot(TypeofMode::NOT_INSIDE_TYPEOF);
}

void BaselineCompiler::VisitLdaLookupGlobalSlotInsideTypeof() {
  Comment("LdaLookupGlobalSlotInsideTypeof");
  BuildLdaLookupGlobalSlot(TypeofMode::INSIDE_TYPEOF);
}

void BaselineCompiler::VisitStaLookupSlot() {
  Comment("StaLookupSlot");
  TNode<Name> name = HeapConstant<Name>(
      Handle<Name>::cast(bytecode_iterator()->GetConstantForIndexOperand(0)));
  Node* value = accumulator_.value();
  uint8_t raw_flags = bytecode_iterator()->GetFlagOperand(1);

  Node* result;
  LanguageMode language_mode;
  LookupHoistingMode lookup_hoisting_mode;
  interpreter::StoreLookupSlotFlags::Decode(raw_flags, &language_mode,
                                            &lookup_hoisting_mode);
  if (language_mode == LanguageMode::kStrict) {
    DCHECK(lookup_hoisting_mode == LookupHoistingMode::kNormal);
    result = CallRuntime(Runtime::kInlineStoreLookupSlot_Strict, GetContext(),
                         name, value);
  } else {
    DCHECK(language_mode == LanguageMode::kSloppy);
    if (lookup_hoisting_mode == LookupHoistingMode::kNormal) {
      result = CallRuntime(Runtime::kInlineStoreLookupSlot_Sloppy, GetContext(),
                           name, value);
    } else {
      DCHECK(lookup_hoisting_mode == LookupHoistingMode::kLegacySloppy);
      result = CallRuntime(Runtime::kInlineStoreLookupSlot_SloppyHoisting,
                           GetContext(), name, value);
    }
  }
  accumulator_.Bind(result);
}

Node* BaselineCompiler::MaybeBuildInlineLoadNamedProperty(Node* receiver,
                                                          TNode<Name> name,
                                                          FeedbackSlot slot) {
  FeedbackNexus nexus(feedback_vector(), slot);
  if (nexus.IsMonomorphic() && nexus.GetFeedbackExtra()->IsSmi() &&
      nexus.FindFirstMap()) {
    Label load_handler(this), bailout(this, Label::kDeferred);
    Handle<Map> map = handle(nexus.FindFirstMap());
    Smi* handler_smi = Smi::cast(nexus.GetFeedbackExtra());
    uint32_t handler_bits = handler_smi->value();
    LoadHandler::Kind kind = LoadHandler::GetHandlerKind(handler_smi);

    if (kind == LoadHandler::Kind::kModuleExport ||
        kind == LoadHandler::Kind::kApiGetter ||
        kind == LoadHandler::Kind::kApiGetterHolderIsPrototype) {
      return nullptr;
    }

    Node* receiver_map = LoadReceiverMap(receiver);

    // // Check if reciever map matches feedback and isn't deprecated.
    // // TODO(rmcilroy): Make map weak.
    GotoIf(IsDeprecatedMap(receiver_map), &bailout);
    Branch(WordEqual(receiver_map, UntypedHeapConstant(map)), &load_handler,
           &bailout);
    BIND(&bailout);
    { BuildBailout(); }

    BIND(&load_handler);
    switch (kind) {
      case LoadHandler::Kind::kField: {
        bool in_object = LoadHandler::IsInobjectBits::decode(handler_bits);
        bool is_double = LoadHandler::IsDoubleBits::decode(handler_bits);
        unsigned index = LoadHandler::FieldIndexBits::decode(handler_bits);
        unsigned offset = index * kPointerSize;

        if (in_object) {
          if (!is_double) {
            return LoadObjectField(receiver, offset);
          } else {
            Node* double_value;
            if (FLAG_unbox_double_fields) {
              double_value =
                  LoadObjectField(receiver, offset, MachineType::Float64());
            } else {
              Node* mutable_heap_number = LoadObjectField(receiver, offset);
              double_value = LoadHeapNumberValue(mutable_heap_number);
            }
            return AllocateHeapNumberWithValue(double_value);
          }
        } else {
          Node* properties = LoadFastProperties(receiver);
          Node* value = LoadObjectField(properties, offset);
          if (is_double) {
            Node* double_value = LoadHeapNumberValue(value);
            value = AllocateHeapNumberWithValue(double_value);
          }
          return value;
        }
      }
      case LoadHandler::Kind::kNormal: {
        Node* properties = LoadSlowProperties(receiver);
        VARIABLE(var_name_index, MachineType::PointerRepresentation());
        Label found(this, &var_name_index);
        NameDictionaryLookup<NameDictionary>(properties, name, &found,
                                             &var_name_index, &bailout);
        BIND(&found);
        {
          VARIABLE(var_details, MachineRepresentation::kWord32);
          VARIABLE(var_value, MachineRepresentation::kTagged);
          LoadPropertyFromNameDictionary(properties, var_name_index.value(),
                                         &var_details, &var_value);
          Node* value =
              CallGetterIfAccessor(var_value.value(), var_details.value(),
                                   GetContext(), receiver, &bailout);
          return value;
        }
      }
      case LoadHandler::Kind::kNonExistent: {
        return CallRuntime(Runtime::kThrowReferenceError, GetContext(), name);
      }
      case LoadHandler::Kind::kConstant: {
        unsigned descriptor = LoadHandler::DescriptorBits::decode(handler_bits);
        Object* constant = map->instance_descriptors()->GetValue(descriptor);
        if (constant->IsSmi()) {
          return SmiConstant(Smi::cast(constant));
        } else {
          return UntypedHeapConstant(
              Handle<HeapObject>::cast(handle(constant, isolate())));
        }
      }
      case LoadHandler::Kind::kAccessor: {
        unsigned descriptor = LoadHandler::DescriptorBits::decode(handler_bits);
        Handle<HeapObject> accessor_pair = Handle<HeapObject>::cast(handle(
            map->instance_descriptors()->GetValue(descriptor), isolate()));
        Node* getter = LoadObjectField(UntypedHeapConstant(accessor_pair),
                                       AccessorPair::kGetterOffset);

        Callable callable = CodeFactory::Call(isolate());
        return CallJS(callable, GetContext(), getter, receiver);
      }
      case LoadHandler::Kind::kNativeDataProperty: {
        unsigned descriptor = LoadHandler::DescriptorBits::decode(handler_bits);
        Handle<HeapObject> accessor_info = Handle<HeapObject>::cast(handle(
            map->instance_descriptors()->GetValue(descriptor), isolate()));

        Callable callable = CodeFactory::ApiGetter(isolate());
        return CallStub(callable, GetContext(), receiver, receiver,
                        UntypedHeapConstant(accessor_info));
      }
      case LoadHandler::Kind::kProxy: {
        return CallStub(
            Builtins::CallableFor(isolate(), Builtins::kProxyGetProperty),
            GetContext(), receiver, name, receiver);
      }

      case LoadHandler::Kind::kGlobal: {
        // Ensure the property cell doesn't contain the hole.
        Node* value = LoadObjectField(receiver, PropertyCell::kValueOffset);
        Node* details = LoadAndUntagToWord32ObjectField(
            receiver, PropertyCell::kDetailsOffset);
        GotoIf(IsTheHole(value), &bailout);

        return CallGetterIfAccessor(value, details, GetContext(), receiver,
                                    &bailout);
      }
      case LoadHandler::Kind::kInterceptor: {
        return CallRuntime(Runtime::kLoadPropertyWithInterceptor, GetContext(),
                           name, receiver, receiver, SmiConstant(slot.ToInt()),
                           LoadFeedbackVector());
      }
      case LoadHandler::Kind::kModuleExport:
      case LoadHandler::Kind::kApiGetter:
      case LoadHandler::Kind::kApiGetterHolderIsPrototype:
      default:
        UNREACHABLE();
    }
  }
  return nullptr;
}

void BaselineCompiler::VisitLdaNamedProperty() {
  Comment("LdaNamedProperty");
  Node* receiver = LoadRegister(bytecode_iterator()->GetRegisterOperand(0));
  TNode<Name> name = HeapConstant<Name>(
      Handle<Name>::cast(bytecode_iterator()->GetConstantForIndexOperand(1)));
  FeedbackSlot slot = bytecode_iterator()->GetSlotOperand(2);

  Node* result = nullptr;
  if (FLAG_spark_opt) {
    result = MaybeBuildInlineLoadNamedProperty(receiver, name, slot);
  }
  if (result == nullptr) {
    // Fallback to generic builtin.
    result = CallBuiltin(Builtins::kLoadIC, GetContext(), receiver, name,
                         SmiConstant(slot.ToInt()), LoadFeedbackVector());
  }

  accumulator_.Bind(result);
}

void BaselineCompiler::VisitLdaKeyedProperty() {
  Comment("LdaKeyedProperty");
  Node* receiver = LoadRegister(bytecode_iterator()->GetRegisterOperand(0));
  Node* name = accumulator_.value();
  Node* slot = SmiConstant(bytecode_iterator()->GetIndexOperand(1));

  Node* result = CallBuiltin(Builtins::kKeyedLoadIC, GetContext(), receiver,
                             name, slot, LoadFeedbackVector());
  accumulator_.Bind(result);
}

void BaselineCompiler::VisitStaNamedProperty() {
  Comment("StaNamedProperty");
  Node* object = LoadRegister(bytecode_iterator()->GetRegisterOperand(0));
  TNode<Name> name = HeapConstant<Name>(
      Handle<Name>::cast(bytecode_iterator()->GetConstantForIndexOperand(1)));
  Node* slot = SmiConstant(bytecode_iterator()->GetIndexOperand(2));
  Node* value = accumulator_.value();

  Node* result = CallBuiltin(Builtins::kStoreIC, GetContext(), object, name,
                             value, slot, LoadFeedbackVector());
  accumulator_.Bind(result);
}

void BaselineCompiler::VisitStaNamedOwnProperty() {
  Comment("StaNamedOwnProperty");
  Node* object = LoadRegister(bytecode_iterator()->GetRegisterOperand(0));
  TNode<Name> name = HeapConstant<Name>(
      Handle<Name>::cast(bytecode_iterator()->GetConstantForIndexOperand(1)));
  Node* slot = SmiConstant(bytecode_iterator()->GetIndexOperand(2));
  Node* value = accumulator_.value();

  // TODO(ishell): Currently we use StoreOwnIC only for storing properties that
  // already exist in the boilerplate therefore we can use StoreIC.
  Node* result = CallBuiltin(Builtins::kStoreIC, GetContext(), object, name,
                             value, slot, LoadFeedbackVector());
  accumulator_.Bind(result);
}

void BaselineCompiler::VisitStaKeyedProperty() {
  Comment("StaKeyedProperty");
  Node* object = LoadRegister(bytecode_iterator()->GetRegisterOperand(0));
  Node* name = LoadRegister(bytecode_iterator()->GetRegisterOperand(1));
  Node* slot = SmiConstant(bytecode_iterator()->GetIndexOperand(2));
  Node* value = accumulator_.value();

  Node* result = CallBuiltin(Builtins::kKeyedStoreIC, GetContext(), object,
                             name, value, slot, LoadFeedbackVector());
  accumulator_.Bind(result);
}

void BaselineCompiler::VisitLdaModuleVariable() {
  Comment("LdaModuleVariable");
  // TODO(rmcilroy): Implement.
  aborted_ = true;
}

void BaselineCompiler::VisitStaModuleVariable() {
  Comment("StaModuleVariable");
  // TODO(rmcilroy): Implement.
  aborted_ = true;
}

void BaselineCompiler::VisitPushContext() {
  Comment("PushContext");
  Register context_reg = bytecode_iterator()->GetRegisterOperand(0);
  Node* new_context = accumulator_.value();
  Node* old_context = GetContext();
  StoreRegister(context_reg, old_context);
  StoreRegister(Register::current_context(), new_context);
}

void BaselineCompiler::VisitPopContext() {
  Comment("PopContext");
  Node* context = LoadRegister(bytecode_iterator()->GetRegisterOperand(0));
  StoreRegister(Register::current_context(), context);
}

void BaselineCompiler::VisitCreateClosure() {
  Comment("CreateClosure");
  Node* shared = UntypedHeapConstant(Handle<HeapObject>::cast(
      bytecode_iterator()->GetConstantForIndexOperand(0)));
  Node* slot = IntPtrConstant(bytecode_iterator()->GetIndexOperand(1));
  uint8_t raw_flag = bytecode_iterator()->GetFlagOperand(2);
  Node* feedback_vector = LoadFeedbackVector();
  Node* feedback_cell = LoadFeedbackVectorSlot(feedback_vector, slot);

  bool fast_new_closure;
  bool pretenure;
  interpreter::CreateClosureFlags::Decode(raw_flag, &fast_new_closure,
                                          &pretenure);

  Node* result;
  if (fast_new_closure) {
    result = CallBuiltin(Builtins::kFastNewClosure, GetContext(), shared,
                         feedback_cell);
  } else {
    Runtime::FunctionId function_id =
        pretenure ? Runtime::kNewClosure_Tenured : Runtime::kNewClosure;
    result = CallRuntime(function_id, GetContext(), shared, feedback_cell);
  }
  accumulator_.Bind(result);
}

void BaselineCompiler::VisitCreateBlockContext() {
  Comment("CreateBlockContext");
  Node* scope_info = UntypedHeapConstant(Handle<HeapObject>::cast(
      bytecode_iterator()->GetConstantForIndexOperand(0)));
  Node* result =
      CallRuntime(Runtime::kPushBlockContext, GetContext(), scope_info);
  accumulator_.Bind(result);
}

void BaselineCompiler::VisitCreateFunctionContext() {
  Comment("CreateFunctionContext");
  Node* scope_info = UntypedHeapConstant(Handle<HeapObject>::cast(
      bytecode_iterator()->GetConstantForIndexOperand(0)));
  Node* slots =
      Int32Constant(bytecode_iterator()->GetUnsignedImmediateOperand(1));
  Node* result = CallBuiltin(Builtins::kFastNewFunctionContextFunction,
                             GetContext(), scope_info, slots);
  accumulator_.Bind(result);
}

void BaselineCompiler::VisitCreateEvalContext() {
  Comment("CreateEvalContext");
  Node* scope_info = UntypedHeapConstant(Handle<HeapObject>::cast(
      bytecode_iterator()->GetConstantForIndexOperand(0)));
  Node* slots =
      Int32Constant(bytecode_iterator()->GetUnsignedImmediateOperand(1));
  Node* result = CallBuiltin(Builtins::kFastNewFunctionContextEval,
                             GetContext(), scope_info, slots);
  accumulator_.Bind(result);
}

void BaselineCompiler::VisitCreateCatchContext() {
  Comment("CreateCatchContext");
  Node* exception = LoadRegister(bytecode_iterator()->GetRegisterOperand(0));
  Node* name = UntypedHeapConstant(Handle<HeapObject>::cast(
      bytecode_iterator()->GetConstantForIndexOperand(1)));
  Node* scope_info = UntypedHeapConstant(Handle<HeapObject>::cast(
      bytecode_iterator()->GetConstantForIndexOperand(2)));
  Node* result = CallRuntime(Runtime::kPushCatchContext, GetContext(), name,
                             exception, scope_info);
  accumulator_.Bind(result);
}

void BaselineCompiler::VisitCreateWithContext() {
  Comment("CreateWithContext");
  Node* object = LoadRegister(bytecode_iterator()->GetRegisterOperand(0));
  Node* scope_info = UntypedHeapConstant(Handle<HeapObject>::cast(
      bytecode_iterator()->GetConstantForIndexOperand(1)));
  Node* result =
      CallRuntime(Runtime::kPushWithContext, GetContext(), object, scope_info);
  accumulator_.Bind(result);
}

void BaselineCompiler::VisitCreateMappedArguments() {
  Comment("CreateMappedArguments");
  Node* closure = LoadRegister(Register::function_closure());
  // Check if function has duplicate parameters.
  // TODO(rmcilroy): Remove this check when FastNewSloppyArgumentsStub supports
  // duplicate parameters.
  Node* result;
  if (!has_duplicate_parameters()) {
    result =
        CallBuiltin(Builtins::kFastNewSloppyArguments, GetContext(), closure);
  } else {
    result = CallRuntime(Runtime::kNewSloppyArguments_Generic, GetContext(),
                         closure);
  }
  accumulator_.Bind(result);
}

void BaselineCompiler::VisitCreateUnmappedArguments() {
  Comment("CreateUnmappedArguments");
  Node* closure = LoadRegister(Register::function_closure());
  Node* result =
      CallBuiltin(Builtins::kFastNewStrictArguments, GetContext(), closure);
  accumulator_.Bind(result);
}

void BaselineCompiler::VisitCreateRestParameter() {
  Comment("CreateRestParameter");
  Node* closure = LoadRegister(Register::function_closure());
  Node* result =
      CallBuiltin(Builtins::kFastNewRestArguments, GetContext(), closure);
  accumulator_.Bind(result);
}

void BaselineCompiler::VisitCreateRegExpLiteral() {
  Comment("CreateRegExpLiteral");
  Node* pattern = UntypedHeapConstant(Handle<HeapObject>::cast(
      bytecode_iterator()->GetConstantForIndexOperand(0)));
  Node* slot = SmiConstant(bytecode_iterator()->GetIndexOperand(1));
  Node* flags = SmiConstant(bytecode_iterator()->GetFlagOperand(2));
  Node* result = CallBuiltin(Builtins::kCreateRegExpLiteral, GetContext(),
                             LoadFeedbackVector(), slot, pattern, flags);
  accumulator_.Bind(result);
}

void BaselineCompiler::VisitCreateArrayLiteral() {
  Comment("CreateArrayLiteral");
  Node* feedback_vector = LoadFeedbackVector();
  Node* constant_elements = UntypedHeapConstant(Handle<HeapObject>::cast(
      bytecode_iterator()->GetConstantForIndexOperand(0)));
  Node* slot = SmiConstant(bytecode_iterator()->GetIndexOperand(1));
  uint8_t raw_flag = bytecode_iterator()->GetFlagOperand(2);

  bool fast_clone;
  int runtime_flags;
  interpreter::CreateArrayLiteralFlags::Decode(raw_flag, &fast_clone,
                                               &runtime_flags);

  Label end(this);
  Node* result;
  if (fast_clone) {
    // If we can do a fast clone do the fast-path in CreateShallowObjectLiteral.
    result =
        CallBuiltin(Builtins::kCreateShallowArrayLiteralTrackAllocationSites,
                    GetContext(), feedback_vector, slot, constant_elements);
    Goto(&end);
  } else {
    // If we can't do a fast clone, call into the runtime.
    result =
        CallRuntime(Runtime::kCreateArrayLiteral, GetContext(), feedback_vector,
                    slot, constant_elements, SmiConstant(runtime_flags));
    Goto(&end);
  }

  BIND(&end);
  accumulator_.Bind(result);
}

void BaselineCompiler::VisitCreateEmptyArrayLiteral() {
  Comment("CreateEmptyArrayLiteral");
  Node* slot = SmiConstant(bytecode_iterator()->GetIndexOperand(0));
  Node* result = CallBuiltin(Builtins::kCreateEmptyArrayLiteral, GetContext(),
                             LoadFeedbackVector(), slot);
  accumulator_.Bind(result);
}

void BaselineCompiler::VisitCreateObjectLiteral() {
  Comment("CreateObjectLiteral");
  Node* feedback_vector = LoadFeedbackVector();
  Node* boilerplate_description = UntypedHeapConstant(Handle<HeapObject>::cast(
      bytecode_iterator()->GetConstantForIndexOperand(0)));
  Node* slot = SmiConstant(bytecode_iterator()->GetIndexOperand(1));
  uint8_t raw_flag = bytecode_iterator()->GetFlagOperand(2);

  bool fast_clone;
  int runtime_flags;
  interpreter::CreateObjectLiteralFlags::Decode(raw_flag, &fast_clone,
                                                &runtime_flags);

  Label end(this);
  Node* result;
  if (fast_clone) {
    // If we can do a fast clone do the fast-path in CreateShallowObjectLiteral.
    result = CallBuiltin(Builtins::kCreateShallowObjectLiteral, GetContext(),
                         feedback_vector, slot, boilerplate_description,
                         SmiConstant(runtime_flags));
    Goto(&end);
  } else {
    // If we can't do a fast clone, call into the runtime.
    result = CallRuntime(Runtime::kCreateObjectLiteral, GetContext(),
                         feedback_vector, slot, boilerplate_description,
                         SmiConstant(runtime_flags));
    Goto(&end);
  }

  BIND(&end);
  Register result_reg = bytecode_iterator()->GetRegisterOperand(3);
  StoreRegister(result_reg, result);
}

void BaselineCompiler::VisitCreateEmptyObjectLiteral() {
  Comment("CreateEmptyObjectLiteral");
  Node* result = CallBuiltin(Builtins::kCreateEmptyObjectLiteral, GetContext());
  accumulator_.Bind(result);
}

void BaselineCompiler::VisitGetTemplateObject() {
  Comment("GetTemplateObject");
  Node* feedback_vector = LoadFeedbackVector();
  Node* slot = IntPtrConstant(bytecode_iterator()->GetIndexOperand(1));
  Node* cached_value =
      LoadFeedbackVectorSlot(feedback_vector, slot, 0, INTPTR_PARAMETERS);

  Label call_runtime(this, Label::kDeferred), end(this);
  GotoIf(WordEqual(cached_value, SmiConstant(0)), &call_runtime);

  accumulator_.Bind(cached_value);
  Goto(&end);

  BIND(&call_runtime);
  {
    Node* description = UntypedHeapConstant(Handle<HeapObject>::cast(
        bytecode_iterator()->GetConstantForIndexOperand(0)));
    Node* result =
        CallRuntime(Runtime::kCreateTemplateObject, GetContext(), description);
    StoreFeedbackVectorSlot(feedback_vector, slot, result, UPDATE_WRITE_BARRIER,
                            0, INTPTR_PARAMETERS);
    accumulator_.Bind(result);
    Goto(&end);
  }
  BIND(&end);
}

Node* BaselineCompiler::BuildCall(ConvertReceiverMode receiver_mode,
                                  Node* target, Register arg_reg,
                                  uint32_t arg_count) {
  Node* receiver;
  if (receiver_mode == ConvertReceiverMode::kNullOrUndefined) {
    receiver = UndefinedConstant();
  } else {
    receiver = LoadRegister(arg_reg);
    arg_reg = Register(arg_reg.index() + 1);
    arg_count -= 1;
  }

  Callable callable = CodeFactory::Call(isolate(), receiver_mode);
  // +5 for stub, target, arg_count, reciever and context
  int input_count = arg_count + 5;
  int index = 0;
  Node** inputs = zone()->NewArray<Node*>(input_count);

  inputs[index++] = HeapConstant(callable.code());
  inputs[index++] = target;
  inputs[index++] = Int32Constant(arg_count);
  inputs[index++] = receiver;
  for (uint32_t i = 0; i < arg_count; i++) {
    inputs[index++] = LoadRegister(arg_reg);
    arg_reg = Register(arg_reg.index() + 1);
  }
  inputs[index++] = GetContext();

  return CallStubN(callable.descriptor(), 1, input_count, inputs);
}

Node* BaselineCompiler::BuildCall(ConvertReceiverMode receiver_mode) {
  Node* target = LoadRegister(bytecode_iterator()->GetRegisterOperand(0));
  Register arg_reg = bytecode_iterator()->GetRegisterOperand(1);
  uint32_t arg_count = bytecode_iterator()->GetRegisterCountOperand(2);
  uint32_t slot_index = bytecode_iterator()->GetIndexOperand(3);
  Node* receiver;
  if (receiver_mode == ConvertReceiverMode::kNullOrUndefined) {
    receiver = UndefinedConstant();
  } else {
    receiver = LoadRegister(arg_reg);
    arg_reg = Register(arg_reg.index() + 1);
    arg_count -= 1;
  }

  Callable callable = CodeFactory::CallWithFeedback(isolate(), receiver_mode);
  // +7 for stub, target, arg_count, feedback_vector, slot, reciever and context
  int input_count = arg_count + 7;
  int index = 0;
  Node** inputs = zone()->NewArray<Node*>(input_count);

  inputs[index++] = HeapConstant(callable.code());
  inputs[index++] = target;
  inputs[index++] = Int32Constant(arg_count);
  inputs[index++] = LoadFeedbackVector();
  inputs[index++] = IntPtrConstant(slot_index);
  inputs[index++] = receiver;
  for (uint32_t i = 0; i < arg_count; i++) {
    inputs[index++] = LoadRegister(arg_reg);
    arg_reg = Register(arg_reg.index() + 1);
  }
  inputs[index++] = GetContext();

  return CallStubN(callable.descriptor(), 1, input_count, inputs);
}

template <size_t... I>
Node* BaselineCompiler::BuildCall(ConvertReceiverMode receiver_mode,
                                  std::index_sequence<I...>) {
  const int kReceiverOperandCount =
      (receiver_mode == ConvertReceiverMode::kNullOrUndefined) ? 0 : 1;
  const int kFirstArgOperandIndex = 1 + kReceiverOperandCount;
  const int kArgCount = static_cast<int>(sizeof...(I));
  const int kSlotOperandIndex = kFirstArgOperandIndex + kArgCount;
  Callable callable = CodeFactory::CallWithFeedback(isolate(), receiver_mode);
  Node* target = LoadRegister(bytecode_iterator()->GetRegisterOperand(0));

  // Load the receiver.
  Node* receiver;
  if (receiver_mode == ConvertReceiverMode::kNullOrUndefined) {
    receiver = UndefinedConstant();
  } else {
    receiver = LoadRegister(bytecode_iterator()->GetRegisterOperand(1));
  }

  // Prepare arguments.
  Node* args[kArgCount];
  for (int i = 0; i < kArgCount; i++) {
    args[i] = LoadRegister(
        bytecode_iterator()->GetRegisterOperand(kFirstArgOperandIndex + i));
  }
  uint32_t slot_index = bytecode_iterator()->GetIndexOperand(kSlotOperandIndex);

  // Call function.
  return CallStub(callable, GetContext(), target, Int32Constant(kArgCount),
                  LoadFeedbackVector(), IntPtrConstant(slot_index), receiver,
                  args[I]...);
}

void BaselineCompiler::VisitCallAnyReceiver() {
  Comment("CallAnyReceiver");
  Node* result = BuildCall(ConvertReceiverMode::kAny);
  accumulator_.Bind(result);
}

void BaselineCompiler::VisitCallProperty() {
  Comment("CallProperty");
  Node* result = BuildCall(ConvertReceiverMode::kNotNullOrUndefined);
  accumulator_.Bind(result);
}

void BaselineCompiler::VisitCallProperty0() {
  Comment("CallProperty0");
  Node* result = BuildCall<0>(ConvertReceiverMode::kNotNullOrUndefined);
  accumulator_.Bind(result);
}

void BaselineCompiler::VisitCallProperty1() {
  Comment("CallProperty1");
  Node* result = BuildCall<1>(ConvertReceiverMode::kNotNullOrUndefined);
  accumulator_.Bind(result);
}

void BaselineCompiler::VisitCallProperty2() {
  Comment("CallProperty2");
  Node* result = BuildCall<2>(ConvertReceiverMode::kNotNullOrUndefined);
  accumulator_.Bind(result);
}

void BaselineCompiler::VisitCallUndefinedReceiver() {
  Comment("CallUndefinedReceiver");
  Node* result = BuildCall(ConvertReceiverMode::kNullOrUndefined);
  accumulator_.Bind(result);
}

void BaselineCompiler::VisitCallUndefinedReceiver0() {
  Comment("CallUndefinedReceiver0");
  Node* result = BuildCall<0>(ConvertReceiverMode::kNullOrUndefined);
  accumulator_.Bind(result);
}

void BaselineCompiler::VisitCallUndefinedReceiver1() {
  Node* result = BuildCall<1>(ConvertReceiverMode::kNullOrUndefined);
  accumulator_.Bind(result);
}

void BaselineCompiler::VisitCallUndefinedReceiver2() {
  Comment("CallUndefinedReceiver2");
  Node* result = BuildCall<2>(ConvertReceiverMode::kNullOrUndefined);
  accumulator_.Bind(result);
}

void BaselineCompiler::VisitCallWithSpread() {
  Comment("CallWithSpread");
  // TODO(rmcilroy): Implement.
  aborted_ = true;
}

void BaselineCompiler::VisitConstruct() {
  Comment("Construct");
  Node* new_target = accumulator_.value();
  Node* target = LoadRegister(bytecode_iterator()->GetRegisterOperand(0));
  Register arg_reg = bytecode_iterator()->GetRegisterOperand(1);
  uint32_t arg_count = bytecode_iterator()->GetRegisterCountOperand(2);
  uint32_t slot_index = bytecode_iterator()->GetIndexOperand(3);

  Callable callable = CodeFactory::ConstructWithFeedback(isolate());
  // +8 for stub, target, new_target, arg_count, feedback vector, slot, context
  // and slot for reciever.
  int input_count = arg_count + 8;
  int index = 0;
  Node** inputs = zone()->NewArray<Node*>(input_count);

  inputs[index++] = HeapConstant(callable.code());
  inputs[index++] = target;
  inputs[index++] = new_target;
  inputs[index++] = Int32Constant(arg_count);
  inputs[index++] = LoadFeedbackVector();
  inputs[index++] = IntPtrConstant(slot_index);
  inputs[index++] = UndefinedConstant();
  for (uint32_t i = 0; i < arg_count; i++) {
    inputs[index++] = LoadRegister(arg_reg);
    arg_reg = Register(arg_reg.index() + 1);
  }
  inputs[index++] = GetContext();
  Node* result = CallStubN(callable.descriptor(), 1, input_count, inputs);
  accumulator_.Bind(result);
}

void BaselineCompiler::VisitConstructWithSpread() {
  Comment("ConstructWithSpread");
  // TODO(rmcilroy): Implement.
  aborted_ = true;
}

void BaselineCompiler::VisitCallJSRuntime() {
  // Get the function to call from the native context.
  uint32_t context_index = bytecode_iterator()->GetNativeContextIndexOperand(0);
  Register arg_reg = bytecode_iterator()->GetRegisterOperand(1);
  uint32_t arg_count = bytecode_iterator()->GetRegisterCountOperand(2);
  Node* context = GetContext();
  Node* native_context = LoadNativeContext(context);
  Node* target = LoadContextElement(native_context, context_index);

  accumulator_.Bind(BuildCall(ConvertReceiverMode::kNullOrUndefined, target,
                              arg_reg, arg_count));
}

Node* BaselineCompiler::BuildCallRuntime() {
  Runtime::FunctionId function_id = bytecode_iterator()->GetRuntimeIdOperand(0);
  Register first_arg_reg = bytecode_iterator()->GetRegisterOperand(1);
  uint32_t arg_count = bytecode_iterator()->GetRegisterCountOperand(2);

  Register arg_reg = first_arg_reg;
  Node* args[arg_count];
  for (uint32_t i = 0; i < arg_count; i++) {
    args[i] = LoadRegister(arg_reg);
    arg_reg = Register(arg_reg.index() + 1);
  }
  return CallRuntimeN(function_id, GetContext(), args, arg_count);
}

void BaselineCompiler::VisitCallRuntime() {
  Comment("CallRuntime");
  Node* result = BuildCallRuntime();
  accumulator_.Bind(result);
}

void BaselineCompiler::VisitCallRuntimeForPair() {
  Comment("CallRuntimeForPair");
  Node* result_pair = BuildCallRuntime();
  Node* result0 = Projection(0, result_pair);
  Node* result1 = Projection(1, result_pair);

  Register first_return_reg = bytecode_iterator()->GetRegisterOperand(3);
  Register second_return_reg = Register(first_return_reg.index() + 1);
  StoreRegister(first_return_reg, result0);
  StoreRegister(second_return_reg, result1);
}

namespace {

Builtins::Name IntrinsicToBuiltin(Runtime::FunctionId function_id) {
  switch (function_id) {
    case Runtime::kInlineCreateIterResultObject:
      return Builtins::kCreateIterResultObject;
    case Runtime::kInlineHasProperty:
      return Builtins::kHasProperty;
    case Runtime::kInlineRejectPromise:
      return Builtins::kRejectPromise;
    case Runtime::kInlineResolvePromise:
      return Builtins::kResolvePromise;
    case Runtime::kInlineToString:
      return Builtins::kToString;
    case Runtime::kInlineToLength:
      return Builtins::kToLength;
    case Runtime::kInlineToInteger:
      return Builtins::kToInteger;
    case Runtime::kInlineToNumber:
      return Builtins::kToNumber;
    case Runtime::kInlineToObject:
      return Builtins::kToObject;
    case Runtime::kInlineCreateJSGeneratorObject:
      return Builtins::kCreateGeneratorObject;
    case Runtime::kInlineCreateAsyncFromSyncIterator:
      return Builtins::kCreateAsyncFromSyncIterator;
    case Runtime::kInlineAsyncFunctionAwaitCaught:
      return Builtins::kAsyncFunctionAwaitCaught;
    case Runtime::kInlineAsyncFunctionAwaitUncaught:
      return Builtins::kAsyncFunctionAwaitUncaught;
    case Runtime::kInlineAsyncGeneratorAwaitCaught:
      return Builtins::kAsyncGeneratorAwaitCaught;
    case Runtime::kInlineAsyncGeneratorAwaitUncaught:
      return Builtins::kAsyncGeneratorAwaitUncaught;
    case Runtime::kInlineAsyncGeneratorReject:
      return Builtins::kAsyncGeneratorReject;
    case Runtime::kInlineAsyncGeneratorResolve:
      return Builtins::kAsyncGeneratorResolve;
    case Runtime::kInlineAsyncGeneratorYield:
      return Builtins::kAsyncGeneratorYield;
    default:
      UNREACHABLE();
  }
}

int IntrinsicToIsInstanceType(Runtime::FunctionId function_id) {
  switch (function_id) {
    case Runtime::kInlineIsArray:
      return JS_ARRAY_TYPE;
    case Runtime::kInlineIsJSProxy:
      return JS_PROXY_TYPE;
    case Runtime::kInlineIsTypedArray:
      return JS_TYPED_ARRAY_TYPE;
    case Runtime::kInlineIsJSMap:
      return JS_MAP_TYPE;
    case Runtime::kInlineIsJSSet:
      return JS_SET_TYPE;
    case Runtime::kInlineIsJSWeakMap:
      return JS_WEAK_MAP_TYPE;
    case Runtime::kInlineIsJSWeakSet:
      return JS_WEAK_SET_TYPE;
    default:
      UNREACHABLE();
  }
}

int IntrinsicLoadObjectOffset(Runtime::FunctionId function_id) {
  switch (function_id) {
    case Runtime::kInlineGeneratorGetInputOrDebugPos:
      return JSGeneratorObject::kInputOrDebugPosOffset;
    case Runtime::kInlineGeneratorGetResumeMode:
      return JSGeneratorObject::kResumeModeOffset;
    default:
      UNREACHABLE();
  }
}

}  // namespace

Node* BaselineCompiler::BuildIntrinsicIsJSReceiver(Register arg) {
  Node* input = LoadRegister(arg);
  TNode<Oddball> result = Select<Oddball>(
      TaggedIsSmi(input), [=] { return FalseConstant(); },
      [=] { return SelectBooleanConstant(IsJSReceiver(input)); });
  return result;
}

Node* BaselineCompiler::BuildIntrinsicIsInstanceType(int type, Register arg) {
  Node* object = LoadRegister(arg);
  TNode<Oddball> result =
      Select<Oddball>(TaggedIsSmi(object), [=] { return FalseConstant(); },
                      [=] {
                        Node* instance_type = LoadInstanceType(object);
                        return SelectBooleanConstant(
                            Word32Equal(instance_type, Int32Constant(type)));
                      });
  return result;
}

Node* BaselineCompiler::BuildIntrinsicAsStubCall(Builtins::Name name,
                                                 Register first_arg_reg,
                                                 uint32_t reg_count) {
  Callable callable = Builtins::CallableFor(isolate_, name);
  int param_count = callable.descriptor().GetParameterCount();
  DCHECK_EQ(reg_count, param_count);
  int input_count = param_count + 2;  // +2 for target and context
  Node** stub_args = zone()->NewArray<Node*>(input_count);
  int index = 0;
  stub_args[index++] = HeapConstant(callable.code());
  Register arg_reg = first_arg_reg;
  for (int i = 0; i < param_count; i++) {
    stub_args[index++] = LoadRegister(arg_reg);
  }
  stub_args[index++] = GetContext();
  return CallStubN(callable.descriptor(), 1, input_count, stub_args);
}

Node* BaselineCompiler::BuildIntrinsicLoadObjectField(Register arg,
                                                      int offset) {
  Node* object = LoadRegister(arg);
  return LoadObjectField(object, offset);
}

Node* BaselineCompiler::BuildIntrinsicGeneratorClose(Register arg) {
  Node* generator = LoadRegister(arg);
  StoreObjectFieldNoWriteBarrier(
      generator, JSGeneratorObject::kContinuationOffset,
      SmiConstant(JSGeneratorObject::kGeneratorClosed));
  return UndefinedConstant();
}

Node* BaselineCompiler::BuildIntrinsicGetImportMetaObject() {
  Node* const module_context = LoadModuleContext(GetContext());
  Node* const module =
      LoadContextElement(module_context, Context::EXTENSION_INDEX);
  Node* const import_meta = LoadObjectField(module, Module::kImportMetaOffset);

  Variable return_value(this, MachineRepresentation::kTagged);
  return_value.Bind(import_meta);

  Label end(this);
  GotoIfNot(IsTheHole(import_meta), &end);

  return_value.Bind(CallRuntime(Runtime::kGetImportMetaObject, GetContext()));
  Goto(&end);

  BIND(&end);
  return return_value.value();
}

Node* BaselineCompiler::BuildIntrinsicCall(Register first_arg_reg,
                                           uint32_t reg_count) {
  Node* target = LoadRegister(first_arg_reg);
  Register call_args_reg = Register(first_arg_reg.index() + 1);
  uint32_t call_arg_count = reg_count - 1;

  return BuildCall(ConvertReceiverMode::kAny, target, call_args_reg,
                   call_arg_count);
}

void BaselineCompiler::VisitInvokeIntrinsic() {
  Comment("InvokeIntrinsic");
  Runtime::FunctionId function_id =
      bytecode_iterator()->GetIntrinsicIdOperand(0);
  Register first_arg_reg = bytecode_iterator()->GetRegisterOperand(1);
  uint32_t reg_count = bytecode_iterator()->GetRegisterCountOperand(2);

  Node* result;
  switch (function_id) {
    case Runtime::kInlineIsArray:
    case Runtime::kInlineIsJSProxy:
    case Runtime::kInlineIsTypedArray:
    case Runtime::kInlineIsJSMap:
    case Runtime::kInlineIsJSSet:
    case Runtime::kInlineIsJSWeakMap:
    case Runtime::kInlineIsJSWeakSet:
      result = BuildIntrinsicIsInstanceType(
          IntrinsicToIsInstanceType(function_id), first_arg_reg);
      break;
    case Runtime::kInlineIsJSReceiver:
      result = BuildIntrinsicIsJSReceiver(first_arg_reg);
      break;
    case Runtime::kInlineIsSmi:
      result = SelectBooleanConstant(TaggedIsSmi(LoadRegister(first_arg_reg)));
      break;
    case Runtime::kInlineCreateIterResultObject:
    case Runtime::kInlineHasProperty:
    case Runtime::kInlineRejectPromise:
    case Runtime::kInlineResolvePromise:
    case Runtime::kInlineToString:
    case Runtime::kInlineToLength:
    case Runtime::kInlineToInteger:
    case Runtime::kInlineToNumber:
    case Runtime::kInlineToObject:
    case Runtime::kInlineCreateJSGeneratorObject:
    case Runtime::kInlineCreateAsyncFromSyncIterator:
    case Runtime::kInlineAsyncFunctionAwaitCaught:
    case Runtime::kInlineAsyncFunctionAwaitUncaught:
    case Runtime::kInlineAsyncGeneratorAwaitCaught:
    case Runtime::kInlineAsyncGeneratorAwaitUncaught:
    case Runtime::kInlineAsyncGeneratorReject:
    case Runtime::kInlineAsyncGeneratorResolve:
    case Runtime::kInlineAsyncGeneratorYield:
      result = BuildIntrinsicAsStubCall(IntrinsicToBuiltin(function_id),
                                        first_arg_reg, reg_count);
      break;
    case Runtime::kInlineGeneratorGetInputOrDebugPos:
    case Runtime::kInlineGeneratorGetResumeMode:
      result = BuildIntrinsicLoadObjectField(
          first_arg_reg, IntrinsicLoadObjectOffset(function_id));
      break;
    case Runtime::kInlineGeneratorClose:
      result = BuildIntrinsicGeneratorClose(first_arg_reg);
      break;
    case Runtime::kInlineGetImportMetaObject:
      result = BuildIntrinsicGetImportMetaObject();
      break;
    case Runtime::kInlineCall:
      result = BuildIntrinsicCall(first_arg_reg, reg_count);
      break;
    default:
      UNREACHABLE();
  }
  accumulator_.Bind(result);
}

void BaselineCompiler::VisitThrow() {
  Comment("Throw");
  Node* exception = accumulator_.value();
  CallRuntime(Runtime::kInlineThrow, GetContext(), exception);
  // We shouldn't ever return from a throw.
  Abort(AbortReason::kUnexpectedReturnFromThrow);
}

void BaselineCompiler::VisitReThrow() {
  Comment("ReThrow");
  Node* exception = accumulator_.value();
  CallRuntime(Runtime::kInlineReThrow, GetContext(), exception);
  // We shouldn't ever return from a throw.
  Abort(AbortReason::kUnexpectedReturnFromThrow);
}

void BaselineCompiler::VisitAbort() {
  Node* reason = SmiConstant(bytecode_iterator()->GetIndexOperand(0));
  CallRuntime(Runtime::kInlineAbort, NoContextConstant(), reason);
  Unreachable();
}

void BaselineCompiler::VisitThrowReferenceErrorIfHole() {
  Comment("ThrowReferenceErrorIfHole");
  Node* value = accumulator_.value();

  Label done(this), throw_error(this, Label::kDeferred);
  Branch(WordEqual(value, TheHoleConstant()), &throw_error, &done);

  BIND(&throw_error);
  {
    TNode<Name> name = HeapConstant<Name>(
        Handle<Name>::cast(bytecode_iterator()->GetConstantForIndexOperand(0)));
    CallRuntime(Runtime::kInlineThrowReferenceError, GetContext(), name);
    // We shouldn't ever return from a throw.
    Abort(AbortReason::kUnexpectedReturnFromThrow);
  }

  BIND(&done);
}

void BaselineCompiler::VisitThrowSuperNotCalledIfHole() {
  Comment("ThrowSuperNotCalledIfHole");
  Node* value = accumulator_.value();

  Label done(this), throw_error(this, Label::kDeferred);
  Branch(WordEqual(value, TheHoleConstant()), &throw_error, &done);

  BIND(&throw_error);
  {
    CallRuntime(Runtime::kInlineThrowSuperNotCalled, GetContext());
    // We shouldn't ever return from a throw.
    Abort(AbortReason::kUnexpectedReturnFromThrow);
  }

  BIND(&done);
}

void BaselineCompiler::VisitThrowSuperAlreadyCalledIfNotHole() {
  Comment("ThrowSuperAlreadyCalledIfNotHole");
  Node* value = accumulator_.value();

  Label done(this), throw_error(this, Label::kDeferred);
  Branch(WordEqual(value, TheHoleConstant()), &done, &throw_error);

  BIND(&throw_error);
  {
    CallRuntime(Runtime::kInlineThrowSuperAlreadyCalledError, GetContext());
    // We shouldn't ever return from a throw.
    Abort(AbortReason::kUnexpectedReturnFromThrow);
  }

  BIND(&done);
}

Node* BaselineCompiler::MaybeBuildInlineUnaryOp(Builtins::Name builtin_id,
                                                Node* operand,
                                                FeedbackSlot slot) {
  FeedbackNexus nexus(feedback_vector(), slot);
  BinaryOperationHint hint = nexus.GetBinaryOperationFeedback();
  if (hint == BinaryOperationHint::kSignedSmall) {
    Label do_unary_op(this), bailout(this, Label::kDeferred);
    Branch(TaggedIsSmi(operand), &do_unary_op, &bailout);

    BIND(&bailout);
    { BuildBailout(); }

    // TODO(rmcilroy): use Token instead.
    BIND(&do_unary_op);
    switch (builtin_id) {
      case Builtins::kBitwiseNotWithFeedback: {
        Node* operand32 = SmiToInt32(operand);
        Node* result =
            BitwiseOp(operand32, Int32Constant(-1), Operation::kBitwiseXor);
        // TODO(rmcilroy): Bailout during operation if not Smi.
        GotoIfNot(TaggedIsSmi(result), &bailout);
        return result;
      }
      case Builtins::kIncrementWithFeedback: {
        Node* raw_operand = BitcastTaggedToWord(operand);
        Node* raw_smi_one = BitcastTaggedToWord(SmiConstant(1));
        Node* pair = IntPtrAddWithOverflow(raw_operand, raw_smi_one);
        Node* overflow = Projection(1, pair);
        GotoIf(overflow, &bailout);
        return BitcastWordToTaggedSigned(Projection(0, pair));
      }
      case Builtins::kDecrementWithFeedback: {
        Node* raw_operand = BitcastTaggedToWord(operand);
        Node* raw_smi_one = BitcastTaggedToWord(SmiConstant(1));
        Node* pair = IntPtrSubWithOverflow(raw_operand, raw_smi_one);
        Node* overflow = Projection(1, pair);
        GotoIf(overflow, &bailout);
        return BitcastWordToTaggedSigned(Projection(0, pair));
      }
      case Builtins::kNegateWithFeedback: {
        Node* lhs32 = SmiToInt32(operand);
        Node* rhs32 = Int32Constant(-1);
        Node* pair = Int32MulWithOverflow(lhs32, rhs32);
        Node* overflow = Projection(1, pair);
        GotoIf(overflow, &bailout);

        // If the answer is zero, we may need to bailout to return -0.0,
        // depending on the input.
        Label answer_zero(this), return_result(this);
        Node* answer = Projection(0, pair);
        Node* zero = Int32Constant(0);
        Branch(Word32Equal(answer, zero), &answer_zero, &return_result);

        BIND(&answer_zero);
        {
          GotoIf(Int32LessThan(Word32Or(lhs32, rhs32), zero), &bailout);
          Goto(&return_result);
        }

        BIND(&return_result);
        return ChangeInt32ToTagged(answer);
      }
      default:
        UNREACHABLE();
    }
  }
  return nullptr;
}

void BaselineCompiler::BuildUnaryOp(Builtins::Name builtin_id) {
  Node* operand = accumulator_.value();
  FeedbackSlot slot = bytecode_iterator()->GetSlotOperand(0);

  Node* result = nullptr;
  if (FLAG_spark_opt) {
    result = MaybeBuildInlineUnaryOp(builtin_id, operand, slot);
  }
  if (result == nullptr) {
    // Fallback to generic builtin.
    result = CallBuiltin(builtin_id, GetContext(), operand,
                         IntPtrConstant(slot.ToInt()), LoadFeedbackVector());
  }
  accumulator_.Bind(result);
}

void BaselineCompiler::VisitBitwiseNot() {
  Comment("BitwiseNot");
  BuildUnaryOp(Builtins::kBitwiseNotWithFeedback);
}

void BaselineCompiler::VisitDec() {
  Comment("Dec");
  BuildUnaryOp(Builtins::kDecrementWithFeedback);
}

void BaselineCompiler::VisitInc() {
  Comment("Inc");
  BuildUnaryOp(Builtins::kIncrementWithFeedback);
}

void BaselineCompiler::VisitNegate() {
  Comment("Negate");
  BuildUnaryOp(Builtins::kNegateWithFeedback);
}

Node* BaselineCompiler::MaybeBuildInlineBinaryOp(Builtins::Name builtin_id,
                                                 Node* left, Node* right,
                                                 FeedbackSlot slot) {
  FeedbackNexus nexus(feedback_vector(), slot);
  BinaryOperationHint hint = nexus.GetBinaryOperationFeedback();
  if (hint == BinaryOperationHint::kSignedSmall) {
    Label do_binary_op(this), bailout(this, Label::kDeferred);
    Node* raw_left = BitcastTaggedToWord(left);
    Node* raw_right = BitcastTaggedToWord(right);

    Node* smi_check = BitcastWordToTaggedSigned(WordOr(raw_left, raw_right));
    Branch(TaggedIsSmi(smi_check), &do_binary_op, &bailout);

    BIND(&bailout);
    { BuildBailout(); }

    // TODO(rmcilroy): use Token instead.
    BIND(&do_binary_op);
    switch (builtin_id) {
      case Builtins::kAddWithFeedback: {
        Node* pair = IntPtrAddWithOverflow(raw_left, raw_right);
        Node* overflow = Projection(1, pair);
        GotoIf(overflow, &bailout);
        return BitcastWordToTaggedSigned(Projection(0, pair));
      }
      case Builtins::kSubtractWithFeedback: {
        Node* pair = IntPtrSubWithOverflow(raw_left, raw_right);
        Node* overflow = Projection(1, pair);
        GotoIf(overflow, &bailout);
        return BitcastWordToTaggedSigned(Projection(0, pair));
      }
      case Builtins::kMultiplyWithFeedback: {
        Node* lhs32 = SmiToInt32(left);
        Node* rhs32 = SmiToInt32(right);
        Node* pair = Int32MulWithOverflow(lhs32, rhs32);
        Node* overflow = Projection(1, pair);
        GotoIf(overflow, &bailout);

        // If the answer is zero, we may need to bailout to return -0.0,
        // depending on the input.
        Label answer_zero(this), return_result(this);
        Node* answer = Projection(0, pair);
        Node* zero = Int32Constant(0);
        Branch(Word32Equal(answer, zero), &answer_zero, &return_result);

        BIND(&answer_zero);
        {
          GotoIf(Int32LessThan(Word32Or(lhs32, rhs32), zero), &bailout);
          Goto(&return_result);
        }

        BIND(&return_result);
        return ChangeInt32ToTagged(answer);
      }
      case Builtins::kDivideWithFeedback: {
        return TrySmiDiv(left, right, &bailout);
      }
      case Builtins::kModulusWithFeedback: {
        Node* result = SmiMod(left, right);
        // TODO(rmcilroy): Bailout during modulus if not Smi.
        GotoIfNot(TaggedIsSmi(result), &bailout);
        return result;
      }
      case Builtins::kExponentiateWithFeedback:
        // TODO(rmcilroy): Implement
        return nullptr;
      case Builtins::kBitwiseAndWithFeedback: {
        Node* lhs32 = SmiToInt32(left);
        Node* rhs32 = SmiToInt32(right);
        Node* result = BitwiseOp(lhs32, rhs32, Operation::kBitwiseAnd);
        // TODO(rmcilroy): Bailout during operation if not Smi.
        GotoIfNot(TaggedIsSmi(result), &bailout);
        return result;
      }
      case Builtins::kBitwiseOrWithFeedback: {
        Node* lhs32 = SmiToInt32(left);
        Node* rhs32 = SmiToInt32(right);
        Node* result = BitwiseOp(lhs32, rhs32, Operation::kBitwiseOr);
        // TODO(rmcilroy): Bailout during operation if not Smi.
        GotoIfNot(TaggedIsSmi(result), &bailout);
        return result;
      }
      case Builtins::kBitwiseXorWithFeedback: {
        Node* lhs32 = SmiToInt32(left);
        Node* rhs32 = SmiToInt32(right);
        Node* result = BitwiseOp(lhs32, rhs32, Operation::kBitwiseXor);
        // TODO(rmcilroy): Bailout during operation if not Smi.
        GotoIfNot(TaggedIsSmi(result), &bailout);
        return result;
      }
      case Builtins::kShiftRightWithFeedback: {
        Node* lhs32 = SmiToInt32(left);
        Node* rhs32 = SmiToInt32(right);
        Node* result = BitwiseOp(lhs32, rhs32, Operation::kShiftRight);
        // TODO(rmcilroy): Bailout during operation if not Smi.
        GotoIfNot(TaggedIsSmi(result), &bailout);
        return result;
      }
      case Builtins::kShiftRightLogicalWithFeedback: {
        Node* lhs32 = SmiToInt32(left);
        Node* rhs32 = SmiToInt32(right);
        Node* result = BitwiseOp(lhs32, rhs32, Operation::kShiftRightLogical);
        // TODO(rmcilroy): Bailout during operation if not Smi.
        GotoIfNot(TaggedIsSmi(result), &bailout);
        return result;
      }
      case Builtins::kShiftLeftWithFeedback: {
        Node* lhs32 = SmiToInt32(left);
        Node* rhs32 = SmiToInt32(right);
        Node* result = BitwiseOp(lhs32, rhs32, Operation::kShiftLeft);
        // TODO(rmcilroy): Bailout during operation if not Smi.
        GotoIfNot(TaggedIsSmi(result), &bailout);
        return result;
      }
      default:
        UNREACHABLE();
    }
  }
  return nullptr;
}

void BaselineCompiler::BuildBinaryOp(Builtins::Name builtin_id) {
  Node* left = LoadRegister(bytecode_iterator()->GetRegisterOperand(0));
  Node* right = accumulator_.value();
  FeedbackSlot slot = bytecode_iterator()->GetSlotOperand(1);

  Node* result = nullptr;
  if (FLAG_spark_opt) {
    result = MaybeBuildInlineBinaryOp(builtin_id, left, right, slot);
  }
  if (result == nullptr) {
    // Fallback to generic builtin.
    result = CallBuiltin(builtin_id, GetContext(), left, right,
                         IntPtrConstant(slot.ToInt()), LoadFeedbackVector());
  }
  accumulator_.Bind(result);
}

void BaselineCompiler::VisitAdd() {
  Comment("Add");
  BuildBinaryOp(Builtins::kAddWithFeedback);
}

void BaselineCompiler::VisitSub() {
  Comment("Sub");
  BuildBinaryOp(Builtins::kSubtractWithFeedback);
}

void BaselineCompiler::VisitMul() {
  Comment("Mul");
  BuildBinaryOp(Builtins::kMultiplyWithFeedback);
}

void BaselineCompiler::VisitDiv() {
  Comment("Div");
  BuildBinaryOp(Builtins::kDivideWithFeedback);
}

void BaselineCompiler::VisitMod() {
  Comment("Mod");
  BuildBinaryOp(Builtins::kModulusWithFeedback);
}

void BaselineCompiler::VisitExp() {
  Comment("Exp");
  BuildBinaryOp(Builtins::kExponentiateWithFeedback);
}

void BaselineCompiler::VisitBitwiseOr() {
  Comment("BitwiseOr");
  BuildBinaryOp(Builtins::kBitwiseOrWithFeedback);
}

void BaselineCompiler::VisitBitwiseXor() {
  Comment("BitwiseXor");
  BuildBinaryOp(Builtins::kBitwiseXorWithFeedback);
}

void BaselineCompiler::VisitBitwiseAnd() {
  Comment("BitwiseAnd");
  BuildBinaryOp(Builtins::kBitwiseAndWithFeedback);
}

void BaselineCompiler::VisitShiftLeft() {
  Comment("ShiftLeft");
  BuildBinaryOp(Builtins::kShiftLeftWithFeedback);
}

void BaselineCompiler::VisitShiftRight() {
  Comment("ShiftRight");
  BuildBinaryOp(Builtins::kShiftRightWithFeedback);
}

void BaselineCompiler::VisitShiftRightLogical() {
  Comment("ShiftRightLogical");
  BuildBinaryOp(Builtins::kShiftRightLogicalWithFeedback);
}

void BaselineCompiler::BuildSmiBinaryOp(Builtins::Name builtin_id) {
  Node* left = accumulator_.value();
  Node* right = SmiConstant(bytecode_iterator()->GetImmediateOperand(0));
  FeedbackSlot slot = bytecode_iterator()->GetSlotOperand(1);

  Node* result = nullptr;
  if (FLAG_spark_opt) {
    result = MaybeBuildInlineBinaryOp(builtin_id, left, right, slot);
  }
  if (result == nullptr) {
    // Fallback to generic builtin.
    result = CallBuiltin(builtin_id, GetContext(), left, right,
                         IntPtrConstant(slot.ToInt()), LoadFeedbackVector());
  }
  accumulator_.Bind(result);
}

void BaselineCompiler::VisitAddSmi() {
  Comment("AddSmi");
  BuildSmiBinaryOp(Builtins::kAddWithFeedback);
}

void BaselineCompiler::VisitSubSmi() {
  Comment("SubSmi");
  BuildSmiBinaryOp(Builtins::kSubtractWithFeedback);
}

void BaselineCompiler::VisitMulSmi() {
  Comment("MulSmi");
  BuildSmiBinaryOp(Builtins::kMultiplyWithFeedback);
}

void BaselineCompiler::VisitDivSmi() {
  Comment("DivSmi");
  BuildSmiBinaryOp(Builtins::kDivideWithFeedback);
}

void BaselineCompiler::VisitModSmi() {
  Comment("ModSmi");
  BuildSmiBinaryOp(Builtins::kModulusWithFeedback);
}

void BaselineCompiler::VisitExpSmi() {
  Comment("ExpSmi");
  BuildSmiBinaryOp(Builtins::kExponentiateWithFeedback);
}

void BaselineCompiler::VisitBitwiseOrSmi() {
  Comment("BitwiseOrSmi");
  BuildSmiBinaryOp(Builtins::kBitwiseOrWithFeedback);
}

void BaselineCompiler::VisitBitwiseXorSmi() {
  Comment("BitwiseXorSmi");
  BuildSmiBinaryOp(Builtins::kBitwiseXorWithFeedback);
}

void BaselineCompiler::VisitBitwiseAndSmi() {
  Comment("BitwiseAndSmi");
  BuildSmiBinaryOp(Builtins::kBitwiseAndWithFeedback);
}

void BaselineCompiler::VisitShiftLeftSmi() {
  Comment("ShiftLeftSmi");
  BuildSmiBinaryOp(Builtins::kShiftLeftWithFeedback);
}

void BaselineCompiler::VisitShiftRightSmi() {
  Comment("ShiftRightSmi");
  BuildSmiBinaryOp(Builtins::kShiftRightWithFeedback);
}

void BaselineCompiler::VisitShiftRightLogicalSmi() {
  Comment("ShiftRightLogicalSmi");
  BuildSmiBinaryOp(Builtins::kShiftRightLogicalWithFeedback);
}

void BaselineCompiler::VisitLogicalNot() {
  Comment("LogicalNot");
  Node* value = accumulator_.value();
  Variable result(this, MachineRepresentation::kTagged);
  Label if_true(this), if_false(this), end(this);
  Node* true_value = TrueConstant();
  Node* false_value = FalseConstant();
  Branch(WordEqual(value, true_value), &if_true, &if_false);
  BIND(&if_true);
  {
    result.Bind(false_value);
    Goto(&end);
  }
  BIND(&if_false);
  {
    result.Bind(true_value);
    Goto(&end);
  }
  BIND(&end);
  accumulator_.Bind(result.value());
}

void BaselineCompiler::VisitToBooleanLogicalNot() {
  Comment("ToBooleanLogicalNot");
  Node* value = accumulator_.value();
  Variable result(this, MachineRepresentation::kTagged);
  Label if_true(this), if_false(this), end(this);
  BranchIfToBooleanIsTrue(value, &if_true, &if_false);
  BIND(&if_true);
  {
    result.Bind(FalseConstant());
    Goto(&end);
  }
  BIND(&if_false);
  {
    result.Bind(TrueConstant());
    Goto(&end);
  }
  BIND(&end);
  accumulator_.Bind(result.value());
}

void BaselineCompiler::VisitTypeOf() {
  Comment("TypeOf");
  Node* result = Typeof(accumulator_.value());
  accumulator_.Bind(result);
}

void BaselineCompiler::VisitDeletePropertyStrict() {
  Comment("DeletePropertyStrict");
  Node* object = LoadRegister(bytecode_iterator()->GetRegisterOperand(0));
  Node* key = accumulator_.value();
  Node* result =
      CallBuiltin(Builtins::kDeleteProperty, GetContext(), object, key,
                  SmiConstant(Smi::FromEnum(LanguageMode::kStrict)));
  accumulator_.Bind(result);
}

void BaselineCompiler::VisitDeletePropertySloppy() {
  Comment("DeletePropertySloppy");
  Node* object = LoadRegister(bytecode_iterator()->GetRegisterOperand(0));
  Node* key = accumulator_.value();
  Node* result =
      CallBuiltin(Builtins::kDeleteProperty, GetContext(), object, key,
                  SmiConstant(Smi::FromEnum(LanguageMode::kSloppy)));
  accumulator_.Bind(result);
}

void BaselineCompiler::VisitGetSuperConstructor() {
  Comment("GetSuperConstructor");
  Node* active_function = accumulator_.value();
  Node* result = GetSuperConstructor(GetContext(), active_function);
  StoreRegister(bytecode_iterator()->GetRegisterOperand(0), result);
}

void BaselineCompiler::BuildTest(Builtins::Name builtin_id) {
  Node* left = LoadRegister(bytecode_iterator()->GetRegisterOperand(0));
  Node* right = accumulator_.value();
  Node* slot = IntPtrConstant(bytecode_iterator()->GetIndexOperand(1));

  Node* result = CallBuiltin(builtin_id, GetContext(), left, right, slot,
                             LoadFeedbackVector());
  accumulator_.Bind(result);
}

void BaselineCompiler::VisitTestEqual() {
  Comment("TestEqual");
  BuildTest(Builtins::kEqualWithFeedback);
}

void BaselineCompiler::VisitTestEqualStrict() {
  Comment("TestEqualStrict");
  BuildTest(Builtins::kStrictEqualWithFeedback);
}

void BaselineCompiler::VisitTestLessThan() {
  Comment("TestLessThan");
  BuildTest(Builtins::kLessThanWithFeedback);
}

void BaselineCompiler::VisitTestGreaterThan() {
  Comment("TestGreaterThan");
  BuildTest(Builtins::kGreaterThanWithFeedback);
}

void BaselineCompiler::VisitTestLessThanOrEqual() {
  Comment("TestLessThanOrEqual");
  BuildTest(Builtins::kLessThanOrEqualWithFeedback);
}

void BaselineCompiler::VisitTestGreaterThanOrEqual() {
  Comment("TestGreaterThanOrEqual");
  BuildTest(Builtins::kGreaterThanOrEqualWithFeedback);
}

void BaselineCompiler::VisitTestReferenceEqual() {
  Comment("TestReferenceEqual");
  Node* left = LoadRegister(bytecode_iterator()->GetRegisterOperand(0));
  Node* right = accumulator_.value();
  Node* result = SelectBooleanConstant(WordEqual(left, right));
  accumulator_.Bind(result);
}

void BaselineCompiler::VisitTestIn() {
  Comment("TestIn");
  Node* key = LoadRegister(bytecode_iterator()->GetRegisterOperand(0));
  Node* object = accumulator_.value();
  Node* result = CallBuiltin(Builtins::kHasProperty, GetContext(), key, object);
  accumulator_.Bind(result);
}

void BaselineCompiler::VisitTestInstanceOf() {
  Comment("TestInstanceOf");
  Node* object = LoadRegister(bytecode_iterator()->GetRegisterOperand(0));
  Node* slot = IntPtrConstant(bytecode_iterator()->GetIndexOperand(1));

  Node* callable = accumulator_.value();
  Node* result = CallBuiltin(Builtins::kInstanceOfWithFeedback, GetContext(),
                             object, callable, slot, LoadFeedbackVector());
  accumulator_.Bind(result);
}

void BaselineCompiler::VisitTestUndetectable() {
  Comment("TestUndetectable");
  Label return_false(this), end(this);
  Node* object = accumulator_.value();

  // If the object is an Smi then return false.
  accumulator_.Bind(FalseConstant());
  GotoIf(TaggedIsSmi(object), &end);

  // If it is a HeapObject, load the map and check for undetectable bit.
  Node* result = SelectBooleanConstant(IsUndetectableMap(LoadMap(object)));
  accumulator_.Bind(result);
  Goto(&end);

  BIND(&end);
}

void BaselineCompiler::VisitTestNull() {
  Comment("TestNull");
  Node* object = accumulator_.value();
  Node* result = SelectBooleanConstant(WordEqual(object, NullConstant()));
  accumulator_.Bind(result);
}

void BaselineCompiler::VisitTestUndefined() {
  Comment("TestUndefined");
  Node* object = accumulator_.value();
  Node* result = SelectBooleanConstant(WordEqual(object, UndefinedConstant()));
  accumulator_.Bind(result);
}

void BaselineCompiler::VisitTestTypeOf() {
  Comment("TestTypeOf");
  typedef interpreter::TestTypeOfFlags::LiteralFlag LiteralFlag;
  Node* object = accumulator_.value();
  LiteralFlag literal_flag = interpreter::TestTypeOfFlags::Decode(
      bytecode_iterator()->GetFlagOperand(0));

  Label if_false(this), if_true(this), end(this);
  switch (literal_flag) {
    case LiteralFlag::kNumber: {
      GotoIfNumber(object, &if_true);
      Goto(&if_false);
      break;
    }
    case LiteralFlag::kString: {
      GotoIf(TaggedIsSmi(object), &if_false);
      Branch(IsString(object), &if_true, &if_false);
      break;
    }
    case LiteralFlag::kSymbol: {
      GotoIf(TaggedIsSmi(object), &if_false);
      Branch(IsSymbol(object), &if_true, &if_false);
      break;
    }
    case LiteralFlag::kBoolean: {
      GotoIf(WordEqual(object, TrueConstant()), &if_true);
      Branch(WordEqual(object, FalseConstant()), &if_true, &if_false);
      break;
    }
    case LiteralFlag::kBigInt: {
      GotoIf(TaggedIsSmi(object), &if_false);
      Branch(IsBigInt(object), &if_true, &if_false);
      break;
    }
    case LiteralFlag::kUndefined: {
      GotoIf(TaggedIsSmi(object), &if_false);
      // Check it is not null and the map has the undetectable bit set.
      GotoIf(IsNull(object), &if_false);
      Branch(IsUndetectableMap(LoadMap(object)), &if_true, &if_false);
      break;
    }
    case LiteralFlag::kFunction: {
      GotoIf(TaggedIsSmi(object), &if_false);
      // Check if callable bit is set and not undetectable.
      Node* map_bitfield = LoadMapBitField(LoadMap(object));
      Node* callable_undetectable =
          Word32And(map_bitfield, Int32Constant(Map::IsUndetectableBit::kMask |
                                                Map::IsCallableBit::kMask));
      Branch(Word32Equal(callable_undetectable,
                         Int32Constant(Map::IsCallableBit::kMask)),
             &if_true, &if_false);
      break;
    }
    case LiteralFlag::kObject: {
      GotoIf(TaggedIsSmi(object), &if_false);

      // If the object is null then return true.
      GotoIf(IsNull(object), &if_true);

      // Check if the object is a receiver type and is not undefined or
      // callable.
      Node* map = LoadMap(object);
      GotoIfNot(IsJSReceiverMap(map), &if_false);
      Node* map_bitfield = LoadMapBitField(map);
      Node* callable_undetectable =
          Word32And(map_bitfield, Int32Constant(Map::IsUndetectableBit::kMask |
                                                Map::IsCallableBit::kMask));
      Branch(Word32Equal(callable_undetectable, Int32Constant(0)), &if_true,
             &if_false);
      break;
    }
    case LiteralFlag::kOther: {
      // Typeof doesn't return any other string value.
      Goto(&if_false);
      break;
    }
  }

  BIND(&if_false);
  {
    accumulator_.Bind(FalseConstant());
    Goto(&end);
  }
  BIND(&if_true);
  {
    accumulator_.Bind(TrueConstant());
    Goto(&end);
  }
  BIND(&end);
}

void BaselineCompiler::VisitToName() {
  Comment("ToName");
  Node* object = accumulator_.value();
  Node* result = ToName(GetContext(), object);
  StoreRegister(bytecode_iterator()->GetRegisterOperand(0), result);
}

void BaselineCompiler::VisitToObject() {
  Comment("ToObject");
  Node* object = accumulator_.value();
  Node* result = CallBuiltin(Builtins::kToObject, GetContext(), object);
  StoreRegister(bytecode_iterator()->GetRegisterOperand(0), result);
}

void BaselineCompiler::VisitToString() {
  Comment("ToString");
  Node* object = accumulator_.value();
  Node* result = ToString_Inline(GetContext(), object);
  accumulator_.Bind(result);
}

void BaselineCompiler::VisitToNumber() {
  Comment("ToNumber");
  Node* object = accumulator_.value();

  Variable var_type_feedback(this, MachineRepresentation::kTaggedSigned);
  Node* result = ToNumberOrNumeric_Inline(
      GetContext(), object, &var_type_feedback, Object::Conversion::kToNumber);

  // Record the type feedback collected for {object}.
  Node* slot_index = IntPtrConstant(bytecode_iterator()->GetIndexOperand(0));
  UpdateFeedback(var_type_feedback.value(), LoadFeedbackVector(), slot_index);

  accumulator_.Bind(result);
}

void BaselineCompiler::VisitToNumeric() {
  Comment("ToNumeric");
  Node* object = accumulator_.value();

  Variable var_type_feedback(this, MachineRepresentation::kTaggedSigned);
  Node* result = ToNumberOrNumeric_Inline(
      GetContext(), object, &var_type_feedback, Object::Conversion::kToNumeric);

  // Record the type feedback collected for {object}.
  Node* slot_index = IntPtrConstant(bytecode_iterator()->GetIndexOperand(0));
  UpdateFeedback(var_type_feedback.value(), LoadFeedbackVector(), slot_index);

  accumulator_.Bind(result);
}

void BaselineCompiler::BuildUpdateInterruptBudget(int32_t delta) {
  Comment("UpdateInterruptBudget");

  // Make sure we include the current bytecode in the budget calculation.
  delta -= interpreter::Bytecodes::Size(
      bytecode_iterator()->current_bytecode(),
      bytecode_iterator()->current_operand_scale());

  // Update budget by |delta| and check if it reaches zero.
  Node* bytecode_array = LoadRegister(Register::bytecode_array());
  Node* old_budget =
      LoadObjectField(bytecode_array, BytecodeArray::kInterruptBudgetOffset,
                      MachineType::Int32());
  Variable new_budget(this, MachineRepresentation::kWord32,
                      Int32Add(old_budget, Int32Constant(delta)));
  if (delta < 0) {
    // Only check if budget is below zero if delta is negative.
    Node* condition =
        Int32GreaterThanOrEqual(new_budget.value(), Int32Constant(0));
    Label ok(this), interrupt_check(this, Label::kDeferred);
    Branch(condition, &ok, &interrupt_check);

    // Perform interrupt and reset budget.
    BIND(&interrupt_check);
    {
      CallRuntime(Runtime::kInterrupt, GetContext());
      new_budget.Bind(
          Int32Constant(interpreter::Interpreter::InterruptBudget()));
      Goto(&ok);
    }

    BIND(&ok);
  }

  // Update budget.
  StoreObjectFieldNoWriteBarrier(
      bytecode_array, BytecodeArray::kInterruptBudgetOffset, new_budget.value(),
      MachineRepresentation::kWord32);
}

void BaselineCompiler::BuildJump() {
  int target = bytecode_iterator()->GetJumpTargetOffset();
  BuildUpdateInterruptBudget(target - bytecode_iterator()->current_offset());
  Goto(jump_targets()->LabelForTarget(target));
}

void BaselineCompiler::VisitJump() {
  Comment("Jump");
  BuildJump();
}

void BaselineCompiler::VisitJumpConstant() {
  Comment("JumpConstant");
  BuildJump();
}

void BaselineCompiler::BuildJumpIf(bool jump_if_true) {
  Label do_jump(this), dont_jump(this);
  Branch(accumulator_.value(), jump_if_true ? &do_jump : &dont_jump,
         jump_if_true ? &dont_jump : &do_jump);

  BIND(&do_jump);
  BuildJump();

  BIND(&dont_jump);
}

void BaselineCompiler::VisitJumpIfTrue() {
  Comment("JumpIfTrue");
  BuildJumpIfToBoolean(true);
}

void BaselineCompiler::VisitJumpIfTrueConstant() {
  Comment("JumpIfTrueConstant");
  BuildJumpIfToBoolean(true);
}

void BaselineCompiler::VisitJumpIfFalse() {
  Comment("JumpIfFalse");
  BuildJumpIfToBoolean(false);
}

void BaselineCompiler::VisitJumpIfFalseConstant() {
  Comment("JumpIfFalseConstant");
  BuildJumpIfToBoolean(false);
}

void BaselineCompiler::BuildJumpIfToBoolean(bool jump_if_true) {
  Label do_jump(this), dont_jump(this);
  BranchIfToBooleanIsTrue(accumulator_.value(),
                          jump_if_true ? &do_jump : &dont_jump,
                          jump_if_true ? &dont_jump : &do_jump);

  BIND(&do_jump);
  BuildJump();

  BIND(&dont_jump);
}

void BaselineCompiler::VisitJumpIfToBooleanTrue() {
  Comment("JumpIfToBooleanTrue");
  BuildJumpIfToBoolean(true);
}

void BaselineCompiler::VisitJumpIfToBooleanTrueConstant() {
  Comment("JumpIfToBooleanTrueConstant");
  BuildJumpIfToBoolean(true);
}

void BaselineCompiler::VisitJumpIfToBooleanFalse() {
  Comment("JumpIfToBooleanFalse");
  BuildJumpIfToBoolean(false);
}

void BaselineCompiler::VisitJumpIfToBooleanFalseConstant() {
  Comment("JumpIfToBooleanFalseConstant");
  BuildJumpIfToBoolean(false);
}

void BaselineCompiler::VisitJumpIfJSReceiver() {
  Comment("JumpIfJSReceiver");
  Label do_jump(this), dont_jump(this);
  BranchIfJSReceiver(accumulator_.value(), &do_jump, &dont_jump);

  BIND(&do_jump);
  BuildJump();

  BIND(&dont_jump);
}

void BaselineCompiler::VisitJumpIfJSReceiverConstant() {
  Comment("JumpIfJSReceiverConstant");
  VisitJumpIfJSReceiver();
}

void BaselineCompiler::VisitJumpIfIs(Node* value, bool jump_if_equal) {
  Label do_jump(this), dont_jump(this);
  Branch(WordEqual(accumulator_.value(), value),
         jump_if_equal ? &do_jump : &dont_jump,
         jump_if_equal ? &dont_jump : &do_jump);

  BIND(&do_jump);
  BuildJump();

  BIND(&dont_jump);
}

void BaselineCompiler::VisitJumpIfNull() {
  Comment("JumpIfNull");
  VisitJumpIfIs(NullConstant(), true);
}

void BaselineCompiler::VisitJumpIfNullConstant() {
  Comment("JumpIfNullConstant");
  VisitJumpIfIs(NullConstant(), true);
}

void BaselineCompiler::VisitJumpIfNotNull() {
  Comment("JumpIfNotNull");
  VisitJumpIfIs(NullConstant(), false);
}

void BaselineCompiler::VisitJumpIfNotNullConstant() {
  Comment("JumpIfNotNullConstant");
  VisitJumpIfIs(NullConstant(), false);
}

void BaselineCompiler::VisitJumpIfUndefined() {
  Comment("JumpIfUndefined");
  VisitJumpIfIs(UndefinedConstant(), true);
}

void BaselineCompiler::VisitJumpIfUndefinedConstant() {
  Comment("JumpIfUndefinedConstant");
  VisitJumpIfIs(UndefinedConstant(), true);
}

void BaselineCompiler::VisitJumpIfNotUndefined() {
  Comment("JumpIfNotUndefined");
  VisitJumpIfIs(UndefinedConstant(), false);
}

void BaselineCompiler::VisitJumpIfNotUndefinedConstant() {
  Comment("JumpIfNotUndefinedConstant");
  VisitJumpIfIs(UndefinedConstant(), false);
}

void BaselineCompiler::VisitJumpLoop() {
  Comment("JumpLoop");
  BuildJump();
}

void BaselineCompiler::VisitSwitchOnSmiNoFeedback() {
  Comment("SwitchOnSmiNoFeedback");
  Node* switch_index = TruncateIntPtrToInt32(SmiUntag(accumulator_.value()));
  interpreter::JumpTableTargetOffsets offsets =
      bytecode_iterator()->GetJumpTableTargetOffsets();

  Label fallthrough(this);
  Label* labels[offsets.size()];
  int32_t case_values[offsets.size()];
  int index = 0;
  for (const auto& entry : offsets) {
    case_values[index] = entry.case_value;
    labels[index] = jump_targets()->LabelForTarget(entry.target_offset);
    index++;
  }
  DCHECK_EQ(index, offsets.size());

  Switch(switch_index, &fallthrough, case_values, labels, offsets.size());
  BIND(&fallthrough);
}

void BaselineCompiler::VisitStackCheck() {
  Comment("StackCheck");
  //PerformStackCheck(GetContext());
}

void BaselineCompiler::VisitSetPendingMessage() {
  Comment("SetPendingMessage");
  Node* pending_message = ExternalConstant(
      ExternalReference::address_of_pending_message_obj(isolate()));
  Node* previous_message = Load(MachineType::TaggedPointer(), pending_message);
  Node* new_message = accumulator_.value();
  StoreNoWriteBarrier(MachineRepresentation::kTaggedPointer, pending_message,
                      new_message);
  accumulator_.Bind(previous_message);
}

void BaselineCompiler::VisitReturn() {
  Comment("Return");

  // Update profiling count by the number of bytes between the end of the
  // current bytecode and the start of the first one, to simulate backedge to
  // start of function.
  BuildUpdateInterruptBudget(-bytecode_iterator()->current_offset());
  Return(accumulator_.value());
}

void BaselineCompiler::VisitIncBlockCounter() {
  Comment("IncBlockCounter");
  Node* closure = LoadRegister(Register::function_closure());
  Node* coverage_slot = SmiConstant(bytecode_iterator()->GetIndexOperand(0));

  CallRuntime(Runtime::kInlineIncBlockCounter, GetContext(), closure,
              coverage_slot);
}

void BaselineCompiler::VisitDebugger() {
  Comment("Debugger");
  // TODO(rmcilroy): Implement.
  UNREACHABLE();
}

// We cannot create compile from the debugger copy of the bytecode array.
#define DEBUG_BREAK(Name, ...) \
  void BaselineCompiler::Visit##Name() { UNREACHABLE(); }
DEBUG_BREAK_BYTECODE_LIST(DEBUG_BREAK);
#undef DEBUG_BREAK

void BaselineCompiler::VisitForInEnumerate() {
  Comment("ForInEnumerate");
  Node* receiver = LoadRegister(bytecode_iterator()->GetRegisterOperand(0));

  Label if_empty(this), if_runtime(this, Label::kDeferred), end(this);
  Node* receiver_map = CheckEnumCache(receiver, &if_empty, &if_runtime);
  accumulator_.Bind(receiver_map);
  Goto(&end);

  BIND(&if_empty);
  {
    accumulator_.Bind(EmptyFixedArrayConstant());
    Goto(&end);
  }

  BIND(&if_runtime);
  {
    Node* result =
        CallRuntime(Runtime::kForInEnumerate, GetContext(), receiver);
    accumulator_.Bind(result);
    Goto(&end);
  }

  BIND(&end);
}

void BaselineCompiler::VisitForInPrepare() {
  Comment("ForInPrepare");
  Node* enumerator = accumulator_.value();
  Register cache_reg_triple = bytecode_iterator()->GetRegisterOperand(0);
  Node* vector_index = IntPtrConstant(bytecode_iterator()->GetIndexOperand(1));
  Node* feedback_vector = LoadFeedbackVector();

  // The {enumerator} is either a Map or a FixedArray.
  CSA_ASSERT(this, TaggedIsNotSmi(enumerator));

  // Check if we're using an enum cache.
  Label if_fast(this), if_slow(this), end(this);
  Branch(IsMap(enumerator), &if_fast, &if_slow);

  BIND(&if_fast);
  {
    // Load the enumeration length and cache from the {enumerator}.
    Node* enum_length = LoadMapEnumLength(enumerator);
    CSA_ASSERT(this, WordNotEqual(enum_length,
                                  IntPtrConstant(kInvalidEnumCacheSentinel)));
    Node* descriptors = LoadMapDescriptors(enumerator);
    Node* enum_cache =
        LoadObjectField(descriptors, DescriptorArray::kEnumCacheOffset);
    Node* enum_keys = LoadObjectField(enum_cache, EnumCache::kKeysOffset);

    // Check if we have enum indices available.
    Node* enum_indices = LoadObjectField(enum_cache, EnumCache::kIndicesOffset);
    Node* enum_indices_length = LoadAndUntagFixedArrayBaseLength(enum_indices);
    Node* feedback = SelectSmiConstant(
        IntPtrLessThanOrEqual(enum_length, enum_indices_length),
        ForInFeedback::kEnumCacheKeysAndIndices, ForInFeedback::kEnumCacheKeys);
    UpdateFeedback(feedback, feedback_vector, vector_index);

    // Construct the cache info triple.
    StoreRegister(cache_reg_triple, enumerator);
    StoreRegister(Register(cache_reg_triple.index() + 1), enum_keys);
    StoreRegister(Register(cache_reg_triple.index() + 2), SmiTag(enum_length));
    Goto(&end);
  }

  BIND(&if_slow);
  {
    // The {enumerator} is a FixedArray with all the keys to iterate.
    CSA_ASSERT(this, IsFixedArray(enumerator));

    // Record the fact that we hit the for-in slow-path.
    UpdateFeedback(SmiConstant(ForInFeedback::kAny), feedback_vector,
                   vector_index);

    // Construct the cache info triple.
    StoreRegister(cache_reg_triple, enumerator);
    StoreRegister(Register(cache_reg_triple.index() + 1), enumerator);
    StoreRegister(Register(cache_reg_triple.index() + 2),
                  LoadFixedArrayBaseLength(enumerator));
    Goto(&end);
  }

  BIND(&end);
}

void BaselineCompiler::VisitForInNext() {
  Comment("ForInNext");
  Node* receiver = LoadRegister(bytecode_iterator()->GetRegisterOperand(0));
  Node* index = LoadRegister(bytecode_iterator()->GetRegisterOperand(1));
  Register cache_reg_pair = bytecode_iterator()->GetRegisterOperand(2);
  Node* cache_type = LoadRegister(cache_reg_pair);
  Node* cache_array = LoadRegister(Register(cache_reg_pair.index() + 1));
  Node* vector_index = IntPtrConstant(bytecode_iterator()->GetIndexOperand(3));
  Node* feedback_vector = LoadFeedbackVector();

  // Load the next key from the enumeration array.
  Node* key = LoadFixedArrayElement(cache_array, index, 0,
                                    CodeStubAssembler::SMI_PARAMETERS);

  // Check if we can use the for-in fast path potentially using the enum cache.
  Label if_fast(this), if_slow(this, Label::kDeferred), end(this);
  Node* receiver_map = LoadMap(receiver);
  Branch(WordEqual(receiver_map, cache_type), &if_fast, &if_slow);
  BIND(&if_fast);
  {
    // Enum cache in use for {receiver}, the {key} is definitely valid.
    accumulator_.Bind(key);
    Goto(&end);
  }

  BIND(&if_slow);
  {
    // Record the fact that we hit the for-in slow-path.
    UpdateFeedback(SmiConstant(ForInFeedback::kAny), feedback_vector,
                   vector_index);

    // Need to filter the {key} for the {receiver}.
    Node* context = GetContext();
    Node* result = CallBuiltin(Builtins::kForInFilter, context, key, receiver);
    accumulator_.Bind(result);
    Goto(&end);
  }

  BIND(&end);
}

void BaselineCompiler::VisitForInContinue() {
  Comment("ForInContinue");
  Node* index = LoadRegister(bytecode_iterator()->GetRegisterOperand(0));
  Node* cache_length = LoadRegister(bytecode_iterator()->GetRegisterOperand(1));
  // Check if {index} is at {cache_length} already.
  accumulator_.Bind(SelectBooleanConstant(WordNotEqual(index, cache_length)));
}

void BaselineCompiler::VisitForInStep() {
  Comment("ForInStep");
  Node* index = LoadRegister(bytecode_iterator()->GetRegisterOperand(0));
  Node* result = SmiAdd(index, SmiConstant(1));
  accumulator_.Bind(result);
}

void BaselineCompiler::VisitSuspendGenerator() {
  Comment("SuspendGenerator");
  // TODO(rmcilroy): Implement.
  aborted_ = true;
}

void BaselineCompiler::VisitSwitchOnGeneratorState() {
  Comment("SwitchOnGeneratorState");
  // TODO(rmcilroy): Implement.
  aborted_ = true;
}

void BaselineCompiler::VisitResumeGenerator() {
  Comment("ResumeGenerator");
  // TODO(rmcilroy): Implement.
  aborted_ = true;
}

void BaselineCompiler::VisitWide() {
  Comment("Wide");
  // Dealt with by bytecode iterator.
  UNREACHABLE();
}

void BaselineCompiler::VisitExtraWide() {
  Comment("ExtraWide");
  // Dealt with by bytecode iterator.
  UNREACHABLE();
}

void BaselineCompiler::VisitIllegal() {
  Comment("Illegal");
  // Shouldn't be emitted.
  UNREACHABLE();
}

}  // namespace baseline
}  // namespace internal
}  // namespace v8
