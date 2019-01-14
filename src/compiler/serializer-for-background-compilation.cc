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

class SerializerForBackgroundCompilation::Environment : public ZoneObject {
 public:
  explicit Environment(Zone* zone, Isolate* isolate, int register_count,
                       int parameter_count);

  Environment(SerializerForBackgroundCompilation* serializer, Isolate* isolate,
              int register_count, int parameter_count, Hints receiver_state,
              const HintsVector& arguments_state);

  int parameter_count() const { return parameter_count_; }
  int register_count() const { return register_count_; }

  // Clear support when runtime jumps are encountered
  void Clear();

  // Getters for the inferred state.
  Hints& LookupAccumulator();
  Hints& LookupRegister(interpreter::Register the_register);

  // Setters for the inferred state.

  // Will add the value (either constant or map) to the Hints
  void AddAccumulatorHint(Handle<Object> value);
  void ReplaceAccumulatorHint(Handle<Object> value);
  void SetAccumulatorHints(const Hints& new_state);
  void ClearAccumulatorHints();

  void AddRegisterHint(interpreter::Register the_register,
                       Handle<Object> value);
  void SetRegisterHints(interpreter::Register the_register,
                        const Hints& new_state);

 private:
  explicit Environment(Zone* zone)
      : environment_hints(zone),
        register_count_(0),
        parameter_count_(0),
        register_base_(0),
        accumulator_base_(0),
        empty_state_(Hints(zone)) {}
  Zone* zone() const { return zone_; }

  int RegisterToLocalIndex(interpreter::Register the_register) const;

  Zone* zone_;

  // environment_hints contains best-effort guess for state of the registers,
  // the accumulator and the parameters. The structure is inspired by
  // BytecodeGraphBuilder::Environment and looks like:
  // hints: receiver | parameters | registers | accumulator
  // indices:    0         register_base_ accumulator_base_

  HintsVector environment_hints;
  const int register_count_;
  const int parameter_count_;
  const int register_base_;
  const int accumulator_base_;
  static const int kReceiverIndex = 0;
  static const int kParameterBase = 1;
  Hints empty_state_;
};

SerializerForBackgroundCompilation::Environment::Environment(
    Zone* zone, Isolate* isolate, int register_count, int parameter_count)
    : zone_(zone),
      environment_hints(register_count + parameter_count + 2, Hints(zone),
                        zone),
      register_count_(register_count),
      parameter_count_(parameter_count),
      register_base_(parameter_count + 1),
      accumulator_base_(register_base_ + register_count),
      empty_state_(Hints(zone)) {
  // Context
  // TODO(mslekova): Later we might need context_index.
}

SerializerForBackgroundCompilation::Environment::Environment(
    SerializerForBackgroundCompilation* serializer, Isolate* isolate,
    int register_count, int parameter_count, Hints receiver_state,
    const HintsVector& arguments_state)
    : Environment(serializer->zone(), isolate, register_count,
                  parameter_count) {
  environment_hints[kReceiverIndex] = receiver_state;

  // Copy the state of the actually passed arguments.
  for (size_t i = 0; i < arguments_state.size(); ++i) {
    environment_hints[kParameterBase + i] = arguments_state[i];
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
  environment_hints = HintsVector(register_count() + parameter_count() + 2,
                                  empty_state_, zone());
}

Hints& SerializerForBackgroundCompilation::Environment::LookupAccumulator() {
  return environment_hints[accumulator_base_];
}

Hints& SerializerForBackgroundCompilation::Environment::LookupRegister(
    interpreter::Register the_register) {
  if (the_register.is_current_context() || the_register.is_function_closure()) {
    return empty_state_;
  }
  int local_index = RegisterToLocalIndex(the_register);
  DCHECK(static_cast<size_t>(local_index) < environment_hints.size());
  return environment_hints[local_index];
}

void SerializerForBackgroundCompilation::Environment::AddAccumulatorHint(
    Handle<Object> value) {
  environment_hints[accumulator_base_].push_back(value);
}

void SerializerForBackgroundCompilation::Environment::ReplaceAccumulatorHint(
    Handle<Object> value) {
  environment_hints[accumulator_base_].clear();
  AddAccumulatorHint(value);
}

void SerializerForBackgroundCompilation::Environment::ClearAccumulatorHints() {
  SetAccumulatorHints(empty_state_);
}

void SerializerForBackgroundCompilation::Environment::SetAccumulatorHints(
    const Hints& new_state) {
  environment_hints[accumulator_base_] = new_state;
}

void SerializerForBackgroundCompilation::Environment::AddRegisterHint(
    interpreter::Register the_register, Handle<Object> value) {
  int local_index = RegisterToLocalIndex(the_register);
  DCHECK(static_cast<size_t>(local_index) < environment_hints.size());
  environment_hints[local_index].push_back(value);
}

void SerializerForBackgroundCompilation::Environment::SetRegisterHints(
    interpreter::Register the_register, const Hints& new_state) {
  int local_index = RegisterToLocalIndex(the_register);
  DCHECK(static_cast<size_t>(local_index) < environment_hints.size());
  environment_hints[local_index] = new_state;
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
    int parameter_count, const Hints& receiver_state,
    const HintsVector& parameters_state)
    : broker_(broker),
      zone_(zone),
      environment_(new (zone) Environment(
          this, broker->isolate(),
          closure->shared()->GetBytecodeArray()->register_count(),
          parameter_count, receiver_state, parameters_state)),
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
        environment()->Clear();
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
  environment()->ClearAccumulatorHints();

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
  Hints object_state =
      environment()->LookupRegister(iterator->GetRegisterOperand(0));

  environment()->ClearAccumulatorHints();
  for (auto object : object_state) {
    if (!object->IsJSObject()) continue;
    Handle<Map> map(Handle<JSObject>::cast(object)->map(), broker()->isolate());
    environment()->AddAccumulatorHint(map);
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
  Hints state = environment()->LookupRegister(iterator->GetRegisterOperand(0));
  environment()->SetAccumulatorHints(state);
}

void SerializerForBackgroundCompilation::VisitStar(
    interpreter::BytecodeArrayIterator* iterator) {
  Hints state = environment()->LookupAccumulator();
  environment()->SetRegisterHints(iterator->GetRegisterOperand(0), state);
}

void SerializerForBackgroundCompilation::VisitMov(
    interpreter::BytecodeArrayIterator* iterator) {
  Hints state = environment()->LookupRegister(iterator->GetRegisterOperand(0));
  environment()->SetRegisterHints(iterator->GetRegisterOperand(1), state);
}

void SerializerForBackgroundCompilation::VisitCallUndefinedReceiver(
    interpreter::BytecodeArrayIterator* iterator) {
  ProcessCallVarArgs(iterator, ConvertReceiverMode::kNullOrUndefined);
}

void SerializerForBackgroundCompilation::VisitCallUndefinedReceiver0(
    interpreter::BytecodeArrayIterator* iterator) {
  Hints receiver_state(zone());
  receiver_state.push_back(broker()->isolate()->factory()->undefined_value());

  Hints callee_state =
      environment()->LookupRegister(iterator->GetRegisterOperand(0));

  HintsVector parameters_state(zone());

  ProcessCall(callee_state, receiver_state, parameters_state, 2);
}

void SerializerForBackgroundCompilation::VisitCallUndefinedReceiver1(
    interpreter::BytecodeArrayIterator* iterator) {
  Hints receiver_state(zone());
  receiver_state.push_back(broker()->isolate()->factory()->undefined_value());

  Hints callee_state =
      environment()->LookupRegister(iterator->GetRegisterOperand(0));

  Hints arg0 = environment()->LookupRegister(iterator->GetRegisterOperand(1));

  HintsVector parameters_state(zone());
  parameters_state.push_back(arg0);

  ProcessCall(callee_state, receiver_state, parameters_state, 3);
}

void SerializerForBackgroundCompilation::VisitCallUndefinedReceiver2(
    interpreter::BytecodeArrayIterator* iterator) {
  Hints receiver_state(zone());
  receiver_state.push_back(broker()->isolate()->factory()->undefined_value());

  Hints callee_state =
      environment()->LookupRegister(iterator->GetRegisterOperand(0));

  Hints arg0 = environment()->LookupRegister(iterator->GetRegisterOperand(1));
  Hints arg1 = environment()->LookupRegister(iterator->GetRegisterOperand(2));

  HintsVector parameters_state(zone());
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
  Hints callee_state =
      environment()->LookupRegister(iterator->GetRegisterOperand(0));
  Hints receiver_state =
      environment()->LookupRegister(iterator->GetRegisterOperand(1));

  HintsVector parameters_state(zone());

  ProcessCall(callee_state, receiver_state, parameters_state, 3);
}

void SerializerForBackgroundCompilation::VisitCallProperty1(
    interpreter::BytecodeArrayIterator* iterator) {
  Hints callee_state =
      environment()->LookupRegister(iterator->GetRegisterOperand(0));
  Hints receiver_state =
      environment()->LookupRegister(iterator->GetRegisterOperand(1));
  Hints arg0 = environment()->LookupRegister(iterator->GetRegisterOperand(2));

  HintsVector parameters_state(zone());
  parameters_state.push_back(arg0);

  ProcessCall(callee_state, receiver_state, parameters_state, 4);
}

void SerializerForBackgroundCompilation::VisitCallProperty2(
    interpreter::BytecodeArrayIterator* iterator) {
  Hints callee_state =
      environment()->LookupRegister(iterator->GetRegisterOperand(0));
  Hints receiver_state =
      environment()->LookupRegister(iterator->GetRegisterOperand(1));
  Hints arg0 = environment()->LookupRegister(iterator->GetRegisterOperand(2));
  Hints arg1 = environment()->LookupRegister(iterator->GetRegisterOperand(3));

  HintsVector parameters_state(zone());
  parameters_state.push_back(arg0);
  parameters_state.push_back(arg1);

  ProcessCall(callee_state, receiver_state, parameters_state, 5);
}

void SerializerForBackgroundCompilation::ProcessCall(
    const Hints& callee_state, const Hints& receiver_state,
    const HintsVector& parameters_state, int parameter_count) {
  if (callee_state.empty()) return;

  for (auto value : callee_state) {
    if (!value->IsJSFunction()) continue;

    RecurseOnCallee(receiver_state, parameters_state, parameter_count,
                    Handle<JSFunction>::cast(value));
  }

  environment()->ClearAccumulatorHints();
}

void SerializerForBackgroundCompilation::RecurseOnCallee(
    const Hints& receiver_state, const HintsVector& parameters_state,
    int parameter_count, Handle<JSFunction> callee) {
  JSFunctionRef callee_ref(broker(), callee);

  // When we encounter direct recursion, we only want to serialize the callee.
  // In this case we don't want to call the Serializer recursively,
  // as the `closure_` is already being analyzed.
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

  SerializerForBackgroundCompilation child_serializer(
      broker(), zone(), callee, parameter_count, receiver_state,
      parameters_state);

  child_serializer.Run();
}

void SerializerForBackgroundCompilation::ProcessCallVarArgs(
    interpreter::BytecodeArrayIterator* iterator,
    ConvertReceiverMode receiver_mode) {
  Hints callee_state =
      environment()->LookupRegister(iterator->GetRegisterOperand(0));

  interpreter::Register first_reg = iterator->GetRegisterOperand(1);
  size_t reg_count = iterator->GetRegisterCountOperand(2);

  int arg_count = receiver_mode == ConvertReceiverMode::kNullOrUndefined
                      ? static_cast<int>(reg_count)
                      : static_cast<int>(reg_count) - 1;

  Hints receiver_state(zone());
  interpreter::Register first_arg;

  if (receiver_mode == ConvertReceiverMode::kNullOrUndefined) {
    // The receiver is implicit (and undefined), the arguments are in
    // consecutive registers.
    receiver_state.push_back(broker()->isolate()->factory()->undefined_value());
    first_arg = first_reg;
  } else {
    // The receiver is the first register, followed by the arguments in the
    // consecutive registers.
    receiver_state = environment()->LookupRegister(first_reg);
    first_arg = interpreter::Register(first_reg.index() + 1);
  }

  HintsVector arguments_state(zone());
  arguments_state.push_back(callee_state);
  arguments_state.push_back(receiver_state);

  // The function arguments are in consecutive registers.
  int arg_base = first_arg.index();
  for (int i = 0; i < arg_count; ++i) {
    arguments_state.push_back(
        environment()->LookupRegister(interpreter::Register(arg_base + i)));
  }

  ProcessCall(callee_state, receiver_state, arguments_state, 2 + arg_count);
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
    environment()->ClearAccumulatorHints();             \
  }
CLEAR_ACCUMULATOR_LIST(DEFINE_CLEAR_ACCUMULATOR)
#undef DEFINE_CLEAR_ACCUMULATOR

}  // namespace compiler
}  // namespace internal
}  // namespace v8
