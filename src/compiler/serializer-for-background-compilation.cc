// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/serializer-for-background-compilation.h"

#include "src/compiler/js-heap-broker.h"
#include "src/handles-inl.h"
#include "src/interpreter/bytecode-array-iterator.h"
#include "src/interpreter/bytecode-register.h"
#include "src/objects/code.h"
#include "src/objects/shared-function-info-inl.h"
#include "src/property-descriptor.h"
#include "src/zone/zone.h"

namespace v8 {
namespace internal {
namespace compiler {

typedef ZoneVector<Handle<Map>> MapVector;
typedef ZoneVector<Handle<Object>> ValueVector;

struct InferredState {
  enum class Kind { kNone, kMaps, kValuesAndMaps };

  explicit InferredState(Zone* zone)
      : kind(Kind::kNone), maps(MapVector(zone)), values(ValueVector(zone)) {}

  Kind kind;
  MapVector maps;
  ValueVector values;
};

class SerializerForBackgroundCompilation::Environment : public ZoneObject {
 public:
  explicit Environment(Zone* zone, Isolate* isolate, int register_count,
                       int parameter_count, MaybeHandle<JSFunction> closure);

  static Environment* NewFromCall(
      SerializerForBackgroundCompilation* serializer, Isolate* isolate,
      int register_count, int parameter_count, InferredState receiver_state,
      const InferredStateVector& arguments_state, Handle<JSFunction> callee,
      Environment* parent);

  int parameter_count() const { return parameter_count_; }
  int register_count() const { return register_count_; }

  // Clear support when runtime jumps are encountered
  void Clear();

  // Getters for the inferred state.
  // Used by Sta* visitors.
  InferredState& LookupAccumulator();
  InferredState& LookupRegister(interpreter::Register the_register);

  // Setters for the internal machine state.
  // Used by Lda* visitors.

  // Will add the constant value to the InferredState and set its type to
  // "ValuesAndMaps".
  void AddSeenAccumulatorValue(Handle<Object> constant);
  void ReplaceAccumulatorValue(Handle<Object> constant);
  void SetAccumulatorInferredState(const InferredState& new_state);
  void ClearAccumulatorInferredState();

  void AddSeenRegisterValue(interpreter::Register the_register,
                            Handle<Object> constant);
  void SetRegisterInferredState(interpreter::Register the_register,
                                const InferredState& new_state);

  MaybeHandle<JSFunction> closure() { return closure_; }

 private:
  explicit Environment(Zone* zone) : inferred_states(zone) {}
  Zone* zone() const { return zone_; }

  int RegisterToLocalIndex(interpreter::Register the_register) const;

  Zone* zone_;

  // inferred_states contains the currently known state of the registers,
  // the accumulator and the parameters. The structure is inspired by
  // BytecodeGraphBuilder::Environment and looks like:
  // values: receiver | parameters | registers | accumulator
  // indices:    0         register_base_ accumulator_base_

  InferredStateVector inferred_states;
  int register_count_;
  int parameter_count_;
  int register_base_;
  int accumulator_base_;
  static const int receiver_idx_ = 0;
  static const int parameter_base_ = 1;

  MaybeHandle<JSFunction> closure_;
  Environment* parent_;
};

SerializerForBackgroundCompilation::Environment::Environment(
    Zone* zone, Isolate* isolate, int register_count, int parameter_count,
    MaybeHandle<JSFunction> closure)
    : zone_(zone),
      inferred_states(register_count + parameter_count + 1, InferredState(zone),
                      zone),
      register_count_(register_count),
      parameter_count_(parameter_count),
      closure_(closure) {
  // Parameters including the receiver

  // Registers
  register_base_ = parameter_count;

  // Accumulator
  accumulator_base_ = register_base_ + register_count;

  USE(parent_);

  // Context
  // TODO(mslekova): Later we might need context_index.
}

SerializerForBackgroundCompilation::Environment*
SerializerForBackgroundCompilation::Environment::NewFromCall(
    SerializerForBackgroundCompilation* serializer, Isolate* isolate,
    int register_count, int parameter_count, InferredState receiver_state,
    const InferredStateVector& arguments_state, Handle<JSFunction> callee,
    Environment* parent) {
  Environment* env = new (serializer->zone()) Environment(
      serializer->zone(), isolate, register_count, parameter_count, callee);
  env->inferred_states[receiver_idx_] = receiver_state;
  for (size_t i = 0; i < arguments_state.size(); ++i) {
    env->inferred_states[parameter_base_ + i] = arguments_state[i];
  }
  env->parent_ = parent;

  return env;
}

int SerializerForBackgroundCompilation::Environment::RegisterToLocalIndex(
    interpreter::Register the_register) const {
  if (the_register.is_parameter()) {
    return the_register.ToParameterIndex(parameter_count());
  } else {
    return register_base_ + the_register.index();
  }
}

void SerializerForBackgroundCompilation::Environment::Clear() {
  inferred_states = InferredStateVector(
      register_count() + parameter_count() + 1, InferredState(zone()), zone());
}

InferredState&
SerializerForBackgroundCompilation::Environment::LookupAccumulator() {
  return inferred_states[accumulator_base_];
}

InferredState& SerializerForBackgroundCompilation::Environment::LookupRegister(
    interpreter::Register the_register) {
  int local_index = RegisterToLocalIndex(the_register);
  DCHECK(static_cast<size_t>(local_index) < inferred_states.size());
  return inferred_states[local_index];
}

void SerializerForBackgroundCompilation::Environment::AddSeenAccumulatorValue(
    Handle<Object> constant) {
  inferred_states[accumulator_base_].kind = InferredState::Kind::kValuesAndMaps;
  inferred_states[accumulator_base_].values.push_back(constant);
}

void SerializerForBackgroundCompilation::Environment::ReplaceAccumulatorValue(
    Handle<Object> constant) {
  inferred_states[accumulator_base_].values.clear();
  AddSeenAccumulatorValue(constant);
}

void SerializerForBackgroundCompilation::Environment::
    ClearAccumulatorInferredState() {
  InferredState empty_state(zone());
  SetAccumulatorInferredState(empty_state);
}

void SerializerForBackgroundCompilation::Environment::
    SetAccumulatorInferredState(const InferredState& new_state) {
  inferred_states[accumulator_base_] = new_state;
}

void SerializerForBackgroundCompilation::Environment::AddSeenRegisterValue(
    interpreter::Register the_register, Handle<Object> constant) {
  int local_index = RegisterToLocalIndex(the_register);
  DCHECK(static_cast<size_t>(local_index) < inferred_states.size());
  inferred_states[local_index].kind = InferredState::Kind::kValuesAndMaps;
  inferred_states[local_index].values.push_back(constant);
}

void SerializerForBackgroundCompilation::Environment::SetRegisterInferredState(
    interpreter::Register the_register, const InferredState& new_state) {
  int local_index = RegisterToLocalIndex(the_register);
  DCHECK(static_cast<size_t>(local_index) < inferred_states.size());
  inferred_states[local_index] = new_state;
}

SerializerForBackgroundCompilation::SerializerForBackgroundCompilation(
    JSHeapBroker* broker, Zone* zone, Handle<JSFunction> closure,
    Environment* environment)
    : broker_(broker),
      zone_(zone),
      environment_(environment),
      closure_(closure),
      bytecode_array_(
          handle(closure->shared()->GetBytecodeArray(), broker->isolate())) {
  native_context_ = broker->native_context().object();
  script_context_table_ =
      handle(native_context_->script_context_table(), broker->isolate());
  if (!environment_) {
    environment_ = new (zone) Environment(
        zone, broker_->isolate(), bytecode_array()->register_count(),
        bytecode_array()->parameter_count(), closure);
  }
}

void SerializerForBackgroundCompilation::Run() { TraverseBytecode(); }

void SerializerForBackgroundCompilation::TraverseBytecode() {
  interpreter::BytecodeArrayIterator iterator(bytecode_array());

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
    interpreter::BytecodeArrayIterator* iterator) {}

MaybeHandle<Object> SerializerForBackgroundCompilation::GetConstantFromLookup(
    LookupIterator& iterator, bool named) const {
  iterator.TryLookupCachedProperty();

  // TODO(mslekova): Do we need to support other states as well?
  if (iterator.state() != LookupIterator::DATA) {
    return MaybeHandle<Object>();
  }

  // TODO(mslekova): The global object might still end up here
  // (consider `let v = this[name]`, where `this === global`),
  // so this should ultimately check for IsJSGlobalObject.
  if (named) {
    return iterator.GetDataValue();
  }

  // TODO(mslekova): Check the feedback - if we have a PropertyCell,
  // we might load the value from there (as is done currently),
  // but we might as well have a Smi feedback.
  Handle<PropertyCell> property_cell = iterator.GetPropertyCell();
  PropertyDetails property_details = property_cell->property_details();
  Handle<Object> property_cell_value(property_cell->value(),
                                     broker()->isolate());

  if ((!property_details.IsConfigurable() && property_details.IsReadOnly()) ||
      property_details.cell_type() == PropertyCellType::kConstant ||
      property_details.cell_type() == PropertyCellType::kUndefined) {
    return property_cell_value;
  }

  return MaybeHandle<Object>();
}

void SerializerForBackgroundCompilation::VisitLdaGlobal(
    interpreter::BytecodeArrayIterator* iterator) {
  Handle<Name> name(Name::cast(iterator->GetConstantForIndexOperand(0)),
                    broker()->isolate());

  Handle<JSGlobalObject> global_object =
      handle(native_context_->global_object(), broker()->isolate());
  ScriptContextTable::LookupResult result;
  if (ScriptContextTable::Lookup(broker()->isolate(), script_context_table_,
                                 Handle<String>::cast(name), &result)) {
    Handle<Context> script_context = ScriptContextTable::GetContext(
        broker()->isolate(), script_context_table_, result.context_index);

    Object contents = script_context->get(result.slot_index);
    environment()->ReplaceAccumulatorValue(
        handle(contents, broker()->isolate()));
  } else {
    LookupIterator it(broker()->isolate(), global_object, name,
                      LookupIterator::OWN);
    MaybeHandle<Object> maybe_value = GetConstantFromLookup(it);
    if (!maybe_value.is_null()) {
      environment()->ReplaceAccumulatorValue(maybe_value.ToHandleChecked());
    } else {
      environment()->ClearAccumulatorInferredState();
    }
  }
}

void SerializerForBackgroundCompilation::VisitLdaGlobalInsideTypeof(
    interpreter::BytecodeArrayIterator* iterator) {
  VisitLdaGlobal(iterator);
}

void SerializerForBackgroundCompilation::VisitLdaUndefined(
    interpreter::BytecodeArrayIterator* iterator) {
  environment()->ReplaceAccumulatorValue(
      broker()->isolate()->factory()->undefined_value());
}

void SerializerForBackgroundCompilation::VisitLdaNull(
    interpreter::BytecodeArrayIterator* iterator) {
  environment()->ReplaceAccumulatorValue(
      broker()->isolate()->factory()->null_value());
}

void SerializerForBackgroundCompilation::VisitLdaZero(
    interpreter::BytecodeArrayIterator* iterator) {
  Handle<Object> zero = handle(Smi::FromInt(0), broker()->isolate());
  environment()->ReplaceAccumulatorValue(zero);
}

void SerializerForBackgroundCompilation::VisitLdaSmi(
    interpreter::BytecodeArrayIterator* iterator) {
  Handle<Object> smi = handle(Smi::FromInt(iterator->GetImmediateOperand(0)),
                              broker()->isolate());
  environment()->ReplaceAccumulatorValue(smi);
}

void SerializerForBackgroundCompilation::VisitLdaConstant(
    interpreter::BytecodeArrayIterator* iterator) {
  Handle<Object> constant =
      handle(iterator->GetConstantForIndexOperand(0), broker()->isolate());
  environment()->ReplaceAccumulatorValue(constant);
}

void SerializerForBackgroundCompilation::VisitLdaNamedProperty(
    interpreter::BytecodeArrayIterator* iterator) {
  InferredState object_state =
      environment()->LookupRegister(iterator->GetRegisterOperand(0));
  Handle<Name> name(Name::cast(iterator->GetConstantForIndexOperand(1)),
                    broker()->isolate());

  if (object_state.kind == InferredState::Kind::kValuesAndMaps) {
    for (auto object : object_state.values) {
      if (object->IsNullOrUndefined()) continue;

      // TODO(mslekova): Check for non-JSReceiver objects as well.
      LookupIterator it = LookupIterator(broker()->isolate(), object, name);
      MaybeHandle<Object> maybe_value = GetConstantFromLookup(it, true);
      if (!maybe_value.is_null()) {
        environment()->ReplaceAccumulatorValue(maybe_value.ToHandleChecked());
        return;
      }
    }
    environment()->ClearAccumulatorInferredState();
  }
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

void SerializerForBackgroundCompilation::VisitStaNamedProperty(
    interpreter::BytecodeArrayIterator* iterator) {
  // ignore
}

void SerializerForBackgroundCompilation::VisitStaNamedPropertyNoFeedback(
    interpreter::BytecodeArrayIterator* iterator) {
  VisitStaNamedProperty(iterator);
}

void SerializerForBackgroundCompilation::VisitStaNamedOwnProperty(
    interpreter::BytecodeArrayIterator* iterator) {
  VisitStaNamedProperty(iterator);
}

void SerializerForBackgroundCompilation::VisitCallUndefinedReceiver(
    interpreter::BytecodeArrayIterator* iterator) {
  ProcessCallVarArgs(iterator, ConvertReceiverMode::kNullOrUndefined);
}

void SerializerForBackgroundCompilation::VisitCallUndefinedReceiver0(
    interpreter::BytecodeArrayIterator* iterator) {
  InferredState receiver_state(zone());
  // TODO(mslekova): Think if we should encapsulate these as methods
  // in the InferredState class.
  receiver_state.kind = InferredState::Kind::kValuesAndMaps;
  receiver_state.values.push_back(
      broker()->isolate()->factory()->undefined_value());

  InferredState callee_state =
      environment()->LookupRegister(iterator->GetRegisterOperand(0));

  InferredStateVector parameters_state(zone());

  ProcessCall(callee_state, receiver_state, parameters_state, 2);
}

void SerializerForBackgroundCompilation::VisitCallUndefinedReceiver1(
    interpreter::BytecodeArrayIterator* iterator) {
  InferredState receiver_state(zone());
  // TODO(mslekova): Think if we should encapsulate these as methods
  // in the InferredState class.
  receiver_state.kind = InferredState::Kind::kValuesAndMaps;
  receiver_state.values.push_back(
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
  // TODO(mslekova): Think if we should encapsulate these as methods
  // in the InferredState class.
  receiver_state.kind = InferredState::Kind::kValuesAndMaps;
  receiver_state.values.push_back(
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
  ProcessCallVarArgs(iterator, ConvertReceiverMode::kAny);
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

  ProcessCall(callee_state, receiver_state, parameters_state, 2);
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

  ProcessCall(callee_state, receiver_state, parameters_state, 3);
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

  ProcessCall(callee_state, receiver_state, parameters_state, 4);
}

void SerializerForBackgroundCompilation::VisitCreateEmptyObjectLiteral(
    interpreter::BytecodeArrayIterator* iterator) {
  environment()->ClearAccumulatorInferredState();
}

void SerializerForBackgroundCompilation::VisitLdaContextSlot(
    interpreter::BytecodeArrayIterator* iterator) {
  VisitCreateEmptyObjectLiteral(iterator);
}
void SerializerForBackgroundCompilation::VisitLdaImmutableContextSlot(
    interpreter::BytecodeArrayIterator* iterator) {
  VisitCreateEmptyObjectLiteral(iterator);
}
void SerializerForBackgroundCompilation::VisitLdaCurrentContextSlot(
    interpreter::BytecodeArrayIterator* iterator) {
  VisitCreateEmptyObjectLiteral(iterator);
}
void SerializerForBackgroundCompilation::VisitLdaImmutableCurrentContextSlot(
    interpreter::BytecodeArrayIterator* iterator) {
  VisitCreateEmptyObjectLiteral(iterator);
}

void SerializerForBackgroundCompilation::ProcessCall(
    const InferredState& callee_state, const InferredState& receiver_state,
    const InferredStateVector& parameters_state, int arg_count) {
  int register_count = 0;

  if (callee_state.kind != InferredState::Kind::kValuesAndMaps) return;

  Handle<JSFunction> callee;
  for (auto value : callee_state.values) {
    if (!value->IsJSFunction()) continue;

    callee = Handle<JSFunction>::cast(value);

    if (callee->shared()->HasBytecodeArray()) {
      register_count = callee->shared()->GetBytecodeArray()->register_count();
    }
  }

  // No callee was found.
  if (callee.location() == nullptr) {
    return;
  }

  // Skipping direct recursion.
  if (callee.equals(closure_)) {
    return;
  }

  // Skipping indirect recursion.
  JSFunctionRef callee_ref(broker(), callee);
  if (callee_ref.serialized_for_inlining()) {
    return;
  }

  callee_ref.SerializeForInlining();

  if (!callee->shared()->HasBytecodeArray()) {
    return;
  }

  Environment* env = Environment::NewFromCall(
      this, broker()->isolate(), register_count, arg_count, receiver_state,
      parameters_state, callee, environment());

  SerializerForBackgroundCompilation child_serializer(broker(), zone(), callee,
                                                      env);

  child_serializer.TraverseBytecode();
}

void SerializerForBackgroundCompilation::ProcessCallArguments(
    InferredState& callee_state, interpreter::Register receiver,
    size_t reg_count, InferredStateVector& args) {
  InferredState& receiver_state = environment()->LookupRegister(receiver);
  // The receiver is followed by the arguments in the consecutive registers.
  DCHECK_GE(reg_count, 1);
  interpreter::Register first_arg = interpreter::Register(receiver.index() + 1);
  int arg_count = static_cast<int>(reg_count) - 1;

  GetCallArgumentsFromRegisters(callee_state, receiver_state, first_arg,
                                arg_count, args);
  ProcessCall(callee_state, receiver_state, args, 2 + arg_count);
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
    receiver_state.kind = InferredState::Kind::kValuesAndMaps;
    receiver_state.values.push_back(
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

void SerializerForBackgroundCompilation::VisitJumpLoop(
    interpreter::BytecodeArrayIterator* iterator) {
  environment()->Clear();
}

#define DEFINE_SKIPPED_JUMP(name, ...)                  \
  void SerializerForBackgroundCompilation::Visit##name( \
      interpreter::BytecodeArrayIterator* iterator) {   \
    VisitJumpLoop(iterator);                            \
  }
SKIPPED_JUMPS_LIST(DEFINE_SKIPPED_JUMP)
#undef DEFINE_SKIPPED_JUMP

}  // namespace compiler
}  // namespace internal
}  // namespace v8
