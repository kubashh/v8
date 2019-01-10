// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/serializer-for-background-compilation.h"

#include "src/compiler/js-heap-broker.h"
#include "src/handles-inl.h"
#include "src/interpreter/bytecode-array-iterator.h"
#include "src/interpreter/bytecode-register.h"
#include "src/lookup.h"
#include "src/objects/code.h"
#include "src/objects/shared-function-info-inl.h"
#include "src/property-descriptor.h"
#include "src/zone/zone.h"

namespace v8 {
namespace internal {
namespace compiler {

typedef ZoneVector<Handle<Object>> HintsVector;

struct InferredState {
  explicit InferredState(Zone* zone) : hints(HintsVector(zone)) {}

  HintsVector hints;

  bool HasInfo() const { return !hints.empty(); }
};

class SerializerForBackgroundCompilation::Environment : public ZoneObject {
 public:
  explicit Environment(Zone* zone, Isolate* isolate, int register_count,
                       int parameter_count);

  Environment(SerializerForBackgroundCompilation* serializer, Isolate* isolate,
              int register_count, int parameter_count,
              InferredState receiver_state,
              const InferredStateVector& arguments_state);

  int parameter_count() const { return parameter_count_; }
  int register_count() const { return register_count_; }

  // Clear support when runtime jumps are encountered
  void Clear();

  // Getters for the inferred state.
  InferredState& LookupAccumulator();
  InferredState& LookupRegister(interpreter::Register the_register);

  // Setters for the inferred state.

  // Will add the constant value to the InferredState and set its type to
  // "ValuesAndMaps".
  void AddSeenAccumulatorValue(Handle<Object> constant);
  void ReplaceAccumulatorHint(Handle<Object> constant);
  void SetAccumulatorInferredState(const InferredState& new_state);
  void ClearAccumulatorInferredState();

  void AddSeenRegisterValue(interpreter::Register the_register,
                            Handle<Object> constant);
  void SetRegisterInferredState(interpreter::Register the_register,
                                const InferredState& new_state);

 private:
  explicit Environment(Zone* zone)
      : inferred_states(zone),
        register_count_(0),
        parameter_count_(0),
        register_base_(0),
        accumulator_base_(0),
        empty_state_(InferredState(zone)) {}
  Zone* zone() const { return zone_; }

  int RegisterToLocalIndex(interpreter::Register the_register) const;

  Zone* zone_;

  // inferred_states contains best-effort guess for state of the registers,
  // the accumulator and the parameters. The structure is inspired by
  // BytecodeGraphBuilder::Environment and looks like:
  // hints: receiver | parameters | registers | accumulator
  // indices:    0         register_base_ accumulator_base_

  InferredStateVector inferred_states;
  const int register_count_;
  const int parameter_count_;
  const int register_base_;
  const int accumulator_base_;
  static const int kReceiverIndex = 0;
  static const int kParameterBase = 1;
  InferredState empty_state_;
};

SerializerForBackgroundCompilation::Environment::Environment(
    Zone* zone, Isolate* isolate, int register_count, int parameter_count)
    : zone_(zone),
      inferred_states(register_count + parameter_count + 2, InferredState(zone),
                      zone),
      register_count_(register_count),
      parameter_count_(parameter_count),
      register_base_(parameter_count + 1),
      accumulator_base_(register_base_ + register_count),
      empty_state_(InferredState(zone)) {
  // Context
  // TODO(mslekova): Later we might need context_index.
}

SerializerForBackgroundCompilation::Environment::Environment(
    SerializerForBackgroundCompilation* serializer, Isolate* isolate,
    int register_count, int parameter_count, InferredState receiver_state,
    const InferredStateVector& arguments_state)
    : Environment(serializer->zone(), isolate, register_count,
                  parameter_count) {
  inferred_states[kReceiverIndex] = receiver_state;

  // Copy the state of the actually passed arguments.
  for (size_t i = 0; i < arguments_state.size(); ++i) {
    inferred_states[kParameterBase + i] = arguments_state[i];
  }
}

int SerializerForBackgroundCompilation::Environment::RegisterToLocalIndex(
    interpreter::Register the_register) const {
  if (the_register.is_parameter()) {
    return kParameterBase + the_register.ToParameterIndex(parameter_count());
  } else {
    return register_base_ + the_register.index();
  }
}

void SerializerForBackgroundCompilation::Environment::Clear() {
  inferred_states = InferredStateVector(
      register_count() + parameter_count() + 2, empty_state_, zone());
}

InferredState&
SerializerForBackgroundCompilation::Environment::LookupAccumulator() {
  return inferred_states[accumulator_base_];
}

InferredState& SerializerForBackgroundCompilation::Environment::LookupRegister(
    interpreter::Register the_register) {
  if (the_register.is_current_context() || the_register.is_function_closure()) {
    return empty_state_;
  }
  int local_index = RegisterToLocalIndex(the_register);
  DCHECK(static_cast<size_t>(local_index) < inferred_states.size());
  return inferred_states[local_index];
}

void SerializerForBackgroundCompilation::Environment::AddSeenAccumulatorValue(
    Handle<Object> constant) {
  inferred_states[accumulator_base_].hints.push_back(constant);
}

void SerializerForBackgroundCompilation::Environment::ReplaceAccumulatorHint(
    Handle<Object> constant) {
  inferred_states[accumulator_base_].hints.clear();
  AddSeenAccumulatorValue(constant);
}

void SerializerForBackgroundCompilation::Environment::
    ClearAccumulatorInferredState() {
  SetAccumulatorInferredState(empty_state_);
}

void SerializerForBackgroundCompilation::Environment::
    SetAccumulatorInferredState(const InferredState& new_state) {
  inferred_states[accumulator_base_] = new_state;
}

void SerializerForBackgroundCompilation::Environment::AddSeenRegisterValue(
    interpreter::Register the_register, Handle<Object> constant) {
  int local_index = RegisterToLocalIndex(the_register);
  DCHECK(static_cast<size_t>(local_index) < inferred_states.size());
  inferred_states[local_index].hints.push_back(constant);
}

void SerializerForBackgroundCompilation::Environment::SetRegisterInferredState(
    interpreter::Register the_register, const InferredState& new_state) {
  int local_index = RegisterToLocalIndex(the_register);
  DCHECK(static_cast<size_t>(local_index) < inferred_states.size());
  inferred_states[local_index] = new_state;
}

SerializerForBackgroundCompilation::SerializerForBackgroundCompilation(
    JSHeapBroker* broker, Zone* zone, Handle<JSFunction> closure)
    : broker_(broker),
      zone_(zone),
      environment_(new (zone) Environment(
          zone, broker_->isolate(),
          closure->shared()->GetBytecodeArray()->register_count(),
          closure->shared()->GetBytecodeArray()->parameter_count())),
      closure_(closure),
      native_context_(broker->native_context().object()),
      script_context_table_(
          handle(native_context_->script_context_table(), broker->isolate())) {}

SerializerForBackgroundCompilation::SerializerForBackgroundCompilation(
    JSHeapBroker* broker, Zone* zone, Handle<JSFunction> closure,
    int register_count, int arg_count, const InferredState& receiver_state,
    const InferredStateVector& parameters_state)
    : broker_(broker),
      zone_(zone),
      environment_(new (zone) Environment(this, broker->isolate(),
                                          register_count, arg_count,
                                          receiver_state, parameters_state)),
      closure_(closure),
      native_context_(broker->native_context().object()),
      script_context_table_(
          handle(native_context_->script_context_table(), broker->isolate())) {}

void SerializerForBackgroundCompilation::Run() {
  FeedbackVectorRef fv(
      broker(), handle(closure_->feedback_vector(), broker()->isolate()));
  fv.SerializeSlots();

  BytecodeArrayRef bytecode_array(
      broker(),
      handle(closure_->shared()->GetBytecodeArray(), broker()->isolate()));

  JSFunctionRef closure_ref(broker(), closure_);
  closure_ref.Serialize();

  TraverseBytecode();
}

void SerializerForBackgroundCompilation::TraverseBytecode() {
  interpreter::BytecodeArrayIterator iterator(
      handle(closure_->shared()->GetBytecodeArray(), broker()->isolate()));

  for (; !iterator.done(); iterator.Advance()) {
    switch (iterator.current_bytecode()) {
#define DEFINE_BYTECODE_CASE(name)     \
  case interpreter::Bytecode::k##name: \
    Visit##name(&iterator);            \
    break;
      SUPPORTED_BYTECODE_LIST(DEFINE_BYTECODE_CASE)
#undef DEFINE_BYTECODE_CASE
      default: {
        break;
      }
    }
  }
}

void SerializerForBackgroundCompilation::VisitIllegal(
    interpreter::BytecodeArrayIterator* iterator) {
  UNREACHABLE();
}

void SerializerForBackgroundCompilation::VisitWide(
    interpreter::BytecodeArrayIterator* iterator) {
  UNREACHABLE();
}

void SerializerForBackgroundCompilation::VisitExtraWide(
    interpreter::BytecodeArrayIterator* iterator) {
  UNREACHABLE();
}

void SerializerForBackgroundCompilation::VisitLdaGlobal(
    interpreter::BytecodeArrayIterator* iterator) {
  environment()->ClearAccumulatorInferredState();

  Handle<Name> name(Name::cast(iterator->GetConstantForIndexOperand(0)),
                    broker()->isolate());

  Handle<JSGlobalObject> global_object =
      handle(native_context_->global_object(), broker()->isolate());
  ScriptContextTable::LookupResult result;
  if (ScriptContextTable::Lookup(broker()->isolate(), script_context_table_,
                                 Handle<String>::cast(name), &result)) {
    Handle<Context> script_context = ScriptContextTable::GetContext(
        broker()->isolate(), script_context_table_, result.context_index);
    ContextRef sc_ref(broker(), script_context);
    sc_ref.Serialize();

    Object contents = script_context->get(result.slot_index);
    environment()->ReplaceAccumulatorHint(
        handle(contents, broker()->isolate()));
  } else {
    LookupIterator it(broker()->isolate(), global_object, name,
                      LookupIterator::OWN);
    it.TryLookupCachedProperty();

    // TODO(mslekova): Do we need to support other states as well?
    if (it.state() != LookupIterator::DATA) {
      return;
    }

    Handle<PropertyCell> property_cell = it.GetPropertyCell();
    PropertyCellRef pc_ref(broker(), property_cell);

    PropertyDetails property_details = property_cell->property_details();
    Handle<Object> property_cell_value(property_cell->value(),
                                       broker()->isolate());

    if ((!property_details.IsConfigurable() && property_details.IsReadOnly()) ||
        property_details.cell_type() == PropertyCellType::kConstant) {
      if (property_cell_value->IsSmi()) {
        Handle<Smi> sc_index = Handle<Smi>::cast(property_cell_value);

        // TODO(mslekova): How to use sc_index?
        USE(sc_index);
      } else {
        MapRef property_cell_value_map(
            broker(), handle(HeapObject::cast(*property_cell_value)->map(),
                             broker()->isolate()));

        if (property_cell_value_map.is_stable()) {
          environment()->ReplaceAccumulatorHint(
              property_cell_value_map.object());
        }
      }
    }
  }
}

void SerializerForBackgroundCompilation::VisitLdaGlobalInsideTypeof(
    interpreter::BytecodeArrayIterator* iterator) {
  VisitLdaGlobal(iterator);
}

void SerializerForBackgroundCompilation::VisitLdaUndefined(
    interpreter::BytecodeArrayIterator* iterator) {
  environment()->ReplaceAccumulatorHint(
      broker()->isolate()->factory()->undefined_value());
}

void SerializerForBackgroundCompilation::VisitLdaNull(
    interpreter::BytecodeArrayIterator* iterator) {
  environment()->ReplaceAccumulatorHint(
      broker()->isolate()->factory()->null_value());
}

void SerializerForBackgroundCompilation::VisitLdaZero(
    interpreter::BytecodeArrayIterator* iterator) {
  Handle<Object> zero(Smi::FromInt(0), broker()->isolate());
  environment()->ReplaceAccumulatorHint(zero);
}

void SerializerForBackgroundCompilation::VisitLdaSmi(
    interpreter::BytecodeArrayIterator* iterator) {
  Handle<Object> smi(Smi::FromInt(iterator->GetImmediateOperand(0)),
                     broker()->isolate());
  environment()->ReplaceAccumulatorHint(smi);
}

void SerializerForBackgroundCompilation::VisitLdaConstant(
    interpreter::BytecodeArrayIterator* iterator) {
  Handle<Object> constant(iterator->GetConstantForIndexOperand(0),
                          broker()->isolate());
  environment()->ReplaceAccumulatorHint(constant);
}

void SerializerForBackgroundCompilation::VisitLdaKeyedProperty(
    interpreter::BytecodeArrayIterator* iterator) {
  InferredState object_state =
      environment()->LookupRegister(iterator->GetRegisterOperand(0));

  environment()->ClearAccumulatorInferredState();
  if (object_state.HasInfo()) {
    for (auto object : object_state.hints) {
      if (!object->IsJSObject()) continue;
      Handle<Map> map =
          handle(Handle<JSObject>::cast(object)->map(), broker()->isolate());
      environment()->AddSeenAccumulatorValue(map);
    }
  }
}

void SerializerForBackgroundCompilation::VisitLdaNamedProperty(
    interpreter::BytecodeArrayIterator* iterator) {
  VisitLdaKeyedProperty(iterator);
}

void SerializerForBackgroundCompilation::VisitLdaNamedPropertyNoFeedback(
    interpreter::BytecodeArrayIterator* iterator) {
  VisitLdaNamedProperty(iterator);
}

void SerializerForBackgroundCompilation::VisitLdar(
    interpreter::BytecodeArrayIterator* iterator) {
  InferredState state =
      environment()->LookupRegister(iterator->GetRegisterOperand(0));
  environment()->SetAccumulatorInferredState(state);
}

void SerializerForBackgroundCompilation::VisitStar(
    interpreter::BytecodeArrayIterator* iterator) {
  InferredState state = environment()->LookupAccumulator();
  environment()->SetRegisterInferredState(iterator->GetRegisterOperand(0),
                                          state);
}

void SerializerForBackgroundCompilation::VisitMov(
    interpreter::BytecodeArrayIterator* iterator) {
  InferredState state =
      environment()->LookupRegister(iterator->GetRegisterOperand(0));
  environment()->SetRegisterInferredState(iterator->GetRegisterOperand(1),
                                          state);
}

void SerializerForBackgroundCompilation::VisitCallUndefinedReceiver(
    interpreter::BytecodeArrayIterator* iterator) {
  ProcessCallVarArgs(iterator, ConvertReceiverMode::kNullOrUndefined);
}

void SerializerForBackgroundCompilation::VisitCallUndefinedReceiver0(
    interpreter::BytecodeArrayIterator* iterator) {
  InferredState receiver_state(zone());
  receiver_state.hints.push_back(
      broker()->isolate()->factory()->undefined_value());

  InferredState callee_state =
      environment()->LookupRegister(iterator->GetRegisterOperand(0));

  InferredStateVector parameters_state(zone());

  ProcessCall(callee_state, receiver_state, parameters_state, 2);
}

void SerializerForBackgroundCompilation::VisitCallUndefinedReceiver1(
    interpreter::BytecodeArrayIterator* iterator) {
  InferredState receiver_state(zone());
  receiver_state.hints.push_back(
      broker()->isolate()->factory()->undefined_value());

  InferredState callee_state =
      environment()->LookupRegister(iterator->GetRegisterOperand(0));

  InferredState arg0 =
      environment()->LookupRegister(iterator->GetRegisterOperand(1));

  InferredStateVector parameters_state(zone());
  parameters_state.push_back(arg0);

  ProcessCall(callee_state, receiver_state, parameters_state, 3);
}

void SerializerForBackgroundCompilation::VisitCallUndefinedReceiver2(
    interpreter::BytecodeArrayIterator* iterator) {
  InferredState receiver_state(zone());
  receiver_state.hints.push_back(
      broker()->isolate()->factory()->undefined_value());

  InferredState callee_state =
      environment()->LookupRegister(iterator->GetRegisterOperand(0));

  InferredState arg0 =
      environment()->LookupRegister(iterator->GetRegisterOperand(1));
  InferredState arg1 =
      environment()->LookupRegister(iterator->GetRegisterOperand(2));

  InferredStateVector parameters_state(zone());
  parameters_state.push_back(arg0);
  parameters_state.push_back(arg1);

  ProcessCall(callee_state, receiver_state, parameters_state, 4);
}

void SerializerForBackgroundCompilation::VisitCallAnyReceiver(
    interpreter::BytecodeArrayIterator* iterator) {
  ProcessCallVarArgs(iterator, ConvertReceiverMode::kAny);
}

void SerializerForBackgroundCompilation::VisitCallNoFeedback(
    interpreter::BytecodeArrayIterator* iterator) {
  ProcessCallVarArgs(iterator, ConvertReceiverMode::kNullOrUndefined);
}

void SerializerForBackgroundCompilation::VisitCallProperty(
    interpreter::BytecodeArrayIterator* iterator) {
  ProcessCallVarArgs(iterator, ConvertReceiverMode::kNullOrUndefined);
}

void SerializerForBackgroundCompilation::VisitCallProperty0(
    interpreter::BytecodeArrayIterator* iterator) {
  InferredState callee_state =
      environment()->LookupRegister(iterator->GetRegisterOperand(0));
  InferredState receiver_state =
      environment()->LookupRegister(iterator->GetRegisterOperand(1));

  InferredStateVector parameters_state(zone());

  ProcessCall(callee_state, receiver_state, parameters_state, 3);
}

void SerializerForBackgroundCompilation::VisitCallProperty1(
    interpreter::BytecodeArrayIterator* iterator) {
  InferredState callee_state =
      environment()->LookupRegister(iterator->GetRegisterOperand(0));
  InferredState receiver_state =
      environment()->LookupRegister(iterator->GetRegisterOperand(1));
  InferredState arg0 =
      environment()->LookupRegister(iterator->GetRegisterOperand(2));

  InferredStateVector parameters_state(zone());
  parameters_state.push_back(arg0);

  ProcessCall(callee_state, receiver_state, parameters_state, 4);
}

void SerializerForBackgroundCompilation::VisitCallProperty2(
    interpreter::BytecodeArrayIterator* iterator) {
  InferredState callee_state =
      environment()->LookupRegister(iterator->GetRegisterOperand(0));
  InferredState receiver_state =
      environment()->LookupRegister(iterator->GetRegisterOperand(1));
  InferredState arg0 =
      environment()->LookupRegister(iterator->GetRegisterOperand(2));
  InferredState arg1 =
      environment()->LookupRegister(iterator->GetRegisterOperand(3));

  InferredStateVector parameters_state(zone());
  parameters_state.push_back(arg0);
  parameters_state.push_back(arg1);

  ProcessCall(callee_state, receiver_state, parameters_state, 5);
}

void SerializerForBackgroundCompilation::ProcessCall(
    const InferredState& callee_state, const InferredState& receiver_state,
    const InferredStateVector& parameters_state, int arg_count) {
  if (!callee_state.HasInfo()) return;

  for (auto value : callee_state.hints) {
    if (!value->IsJSFunction()) continue;

    RecurseOnCallee(receiver_state, parameters_state, arg_count,
                    Handle<JSFunction>::cast(value));
  }

  environment()->Clear();
}

void SerializerForBackgroundCompilation::RecurseOnCallee(
    const InferredState& receiver_state,
    const InferredStateVector& parameters_state, int arg_count,
    Handle<JSFunction> callee) {
  JSFunctionRef callee_ref(broker(), callee);

  // Only serialize for direct recursion, don't serialize recursively.
  if (callee.equals(closure_)) {
    callee_ref.SerializeForInlining();
    return;
  }

  // Skipping indirect recursion.
  if (callee_ref.serialized_for_inlining()) {
    return;
  }

  callee_ref.SerializeForInlining();

  if (!callee->shared()->HasBytecodeArray()) {
    return;
  }

  int register_count = callee->shared()->GetBytecodeArray()->register_count();

  SerializerForBackgroundCompilation child_serializer(
      broker(), zone(), callee, register_count, arg_count, receiver_state,
      parameters_state);

  child_serializer.TraverseBytecode();
}

void SerializerForBackgroundCompilation::ProcessCallVarArgs(
    interpreter::BytecodeArrayIterator* iterator,
    ConvertReceiverMode receiver_mode) {
  InferredState callee_state =
      environment()->LookupRegister(iterator->GetRegisterOperand(0));

  interpreter::Register first_reg = iterator->GetRegisterOperand(1);
  size_t reg_count = iterator->GetRegisterCountOperand(2);

  int arg_count = receiver_mode == ConvertReceiverMode::kNullOrUndefined
                      ? static_cast<int>(reg_count)
                      : static_cast<int>(reg_count) - 1;

  InferredState receiver_state(zone());
  interpreter::Register first_arg;

  if (receiver_mode == ConvertReceiverMode::kNullOrUndefined) {
    // The receiver is implicit (and undefined), the arguments are in
    // consecutive registers.
    receiver_state.hints.push_back(
        broker()->isolate()->factory()->undefined_value());
    first_arg = first_reg;
  } else {
    // The receiver is the first register, followed by the arguments in the
    // consecutive registers.
    receiver_state = environment()->LookupRegister(first_reg);
    first_arg = interpreter::Register(first_reg.index() + 1);
  }

  InferredStateVector parameters_state(zone());
  GetCallArgumentsFromRegisters(callee_state, receiver_state, first_arg,
                                arg_count, parameters_state);

  ProcessCall(callee_state, receiver_state, parameters_state, 2 + arg_count);
}

void SerializerForBackgroundCompilation::GetCallArgumentsFromRegisters(
    InferredState& callee_state, InferredState& receiver_state,
    interpreter::Register first_arg, int arg_count, InferredStateVector& args) {
  args.push_back(callee_state);
  args.push_back(receiver_state);

  // The function arguments are in consecutive registers.
  int arg_base = first_arg.index();
  for (int i = 0; i < arg_count; ++i) {
    args.push_back(
        environment()->LookupRegister(interpreter::Register(arg_base + i)));
  }
}

#define DEFINE_SKIPPED_JUMP(name, ...)                  \
  void SerializerForBackgroundCompilation::Visit##name( \
      interpreter::BytecodeArrayIterator* iterator) {   \
    environment()->Clear();                             \
  }
CLEAR_ENVIRONMENT_LIST(DEFINE_SKIPPED_JUMP)
#undef DEFINE_SKIPPED_JUMP

#define DEFINE_CLEAR_ACCUMULATOR(name, ...)             \
  void SerializerForBackgroundCompilation::Visit##name( \
      interpreter::BytecodeArrayIterator* iterator) {   \
    environment()->ClearAccumulatorInferredState();     \
  }
CLEAR_ACCUMULATOR_LIST(DEFINE_CLEAR_ACCUMULATOR)
#undef DEFINE_CLEAR_ACCUMULATOR

}  // namespace compiler
}  // namespace internal
}  // namespace v8
