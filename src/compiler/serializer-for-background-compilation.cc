// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/serializer-for-background-compilation.h"

#include "src/compiler/js-heap-broker.h"
// #include "src/feedback-vector.h"
#include "src/handles-inl.h"
#include "src/interpreter/bytecode-array-iterator.h"
#include "src/interpreter/bytecode-register.h"
#include "src/objects/code.h"
#include "src/objects/shared-function-info-inl.h"
// #include "src/source-position-table.h"
// #include "src/vector-slot-pair.h"
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
typedef ZoneVector<InferredState> InferredStateVector;

class SerializerForBackgroundCompilation::Environment : public ZoneObject {
 public:
  explicit Environment(SerializerForBackgroundCompilation* serializer,
                       Isolate* isolate, int register_count,
                       int parameter_count,
                       const interpreter::BytecodeArrayIterator* iterator,
                       MaybeHandle<JSFunction> closure);

  static Environment* NewFromCall(
      SerializerForBackgroundCompilation* serializer, Isolate* isolate,
      const interpreter::BytecodeArrayIterator* iterator,
      MaybeHandle<JSFunction> closure, InferredState callee_state,
      InferredState receiver_state, const InferredStateVector& arguments_state);

  // Getters for the inferred state.
  // Used by Sta* visitors.
  InferredState LookupAccumulator() const;
  InferredState LookupRegister(interpreter::Register the_register) const;

  // Setters for the internal machine state.
  // Used by Lda* visitors.

  // Will add the constant value to the InferredState and set its type to
  // "ValuesAndMaps".
  void AddSeenAccumulatorValue(Handle<Object> constant);
  void SetAccumulatorInferredState(InferredState new_state);

  void AddSeenRegisterValue(interpreter::Register the_register,
                            Handle<Object> constant);
  void SetRegisterInferredState(interpreter::Register the_register,
                                InferredState new_state);

 private:
  explicit Environment(Zone* zone) : inferred_states(zone) {}
  Zone* zone() const { return serializer_->local_zone(); }

  int RegisterToLocalIndex(interpreter::Register the_register) const;

  SerializerForBackgroundCompilation* serializer_;

  // Contains the currently known state of the registers, the accumulator
  // and the parameters. The structure is inspired by
  // BytecodeGraphBuilder::Environment and looks like:
  // values: receiver | parameters | registers | accumulator
  // indices:    0         register_base_ accumulator_base_
  InferredStateVector inferred_states;
  int register_base_;
  int accumulator_base_;
  static const int receiver_idx_ = 0;
  static const int parameter_base_ = 1;

  MaybeHandle<JSFunction> closure_;
};

SerializerForBackgroundCompilation::Environment::Environment(
    SerializerForBackgroundCompilation* serializer, Isolate* isolate,
    int register_count, int parameter_count,
    const interpreter::BytecodeArrayIterator* iterator,
    MaybeHandle<JSFunction> closure)
    : serializer_(serializer),
      inferred_states(register_count + parameter_count + 2,
                      InferredState(serializer->local_zone()),
                      serializer->local_zone()),
      closure_(closure) {
  // Parameters including the receiver

  // Registers
  register_base_ = parameter_count;

  // Accumulator
  accumulator_base_ = register_base_ + register_count;

  // Context
  // TODO(mslekova): Later we might need context_index.
}

SerializerForBackgroundCompilation::Environment*
SerializerForBackgroundCompilation::Environment::NewFromCall(
    SerializerForBackgroundCompilation* serializer, Isolate* isolate,
    const interpreter::BytecodeArrayIterator* iterator,
    MaybeHandle<JSFunction> closure, InferredState callee_state,
    InferredState receiver_state, const InferredStateVector& arguments_state) {
  int register_count = 0;
  int parameter_count = static_cast<int>(arguments_state.size());
  // TODO(mslekova): Copy arguments_state

  MaybeHandle<JSFunction> callee;
  switch (callee_state.kind) {
    case InferredState::Kind::kValuesAndMaps: {
      for (auto value : callee_state.values) {
        if (!value->IsJSFunction()) continue;

        callee = Handle<JSFunction>::cast(value);

        JSFunctionRef callee_ref(serializer->broker(),
                                 callee.ToHandleChecked());
        callee_ref.SerializeForInlining();
      }
      break;
    }
    default: {
      break;
    }
  }

  Environment* env = new (serializer->local_zone()) Environment(
      serializer, isolate, register_count, parameter_count, iterator, callee);
  env->inferred_states[receiver_idx_] = receiver_state;

  return env;
}

int SerializerForBackgroundCompilation::Environment::RegisterToLocalIndex(
    interpreter::Register the_register) const {
  if (the_register.is_parameter()) {
    return the_register.ToParameterIndex(register_base_);
  } else {
    return register_base_ + the_register.index();
  }
}

InferredState
SerializerForBackgroundCompilation::Environment::LookupAccumulator() const {
  return inferred_states[accumulator_base_];
}

InferredState SerializerForBackgroundCompilation::Environment::LookupRegister(
    interpreter::Register the_register) const {
  int local_index = RegisterToLocalIndex(the_register);
  DCHECK(static_cast<size_t>(local_index) < inferred_states.size());
  return inferred_states[local_index];
}

void SerializerForBackgroundCompilation::Environment::AddSeenAccumulatorValue(
    Handle<Object> constant) {
  inferred_states[accumulator_base_].kind = InferredState::Kind::kValuesAndMaps;
  inferred_states[accumulator_base_].values.push_back(constant);
}

void SerializerForBackgroundCompilation::Environment::
    SetAccumulatorInferredState(InferredState new_state) {
  // if (inferred_states[accumulator_base_].kind ==
  // InferredState::Kind::kValuesAndMaps &&
  //   new_state.kind != InferredState::Kind::kValuesAndMaps) {
  //   closure_.ToHandleChecked()->Print();
  //   printf("\n");
  //   CHECK(false);
  // }
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
    interpreter::Register the_register, InferredState new_state) {
  int local_index = RegisterToLocalIndex(the_register);
  DCHECK(static_cast<size_t>(local_index) < inferred_states.size());
  inferred_states[local_index] = new_state;
}

SerializerForBackgroundCompilation::SerializerForBackgroundCompilation(
    JSHeapBroker* broker, Zone* local_zone, Handle<JSFunction> closure)
    : broker_(broker),
      local_zone_(local_zone),
      environment_(nullptr),
      closure_(closure),
      bytecode_array_(
          handle(closure->shared()->GetBytecodeArray(), broker->isolate())) {}

void SerializerForBackgroundCompilation::Run() { TraverseBytecode(); }

void SerializerForBackgroundCompilation::TraverseBytecode() {
  interpreter::BytecodeArrayIterator iterator(bytecode_array());
  set_bytecode_iterator(&iterator);

  Environment env(this, broker_->isolate(), bytecode_array()->register_count(),
                  bytecode_array()->parameter_count(), &iterator, closure_);
  set_environment(&env);

  SourcePositionTableIterator source_position_iterator(
      handle(bytecode_array()->SourcePositionTable(), broker()->isolate()));

  for (; !iterator.done(); iterator.Advance()) {
    VisitBytecode(&source_position_iterator);
  }

  set_bytecode_iterator(nullptr);
}

MaybeHandle<Object> SerializerForBackgroundCompilation::GetConstantFromLookup(
    LookupIterator& iterator, bool named) const {
  iterator.TryLookupCachedProperty();

  // TODO(mslekova): Maybe support other states as well.
  if (iterator.state() == LookupIterator::DATA) {
    if (named) {
      return iterator.GetDataValue();
    } else {
      Handle<PropertyCell> property_cell = iterator.GetPropertyCell();
      PropertyDetails property_details = property_cell->property_details();
      Handle<Object> property_cell_value(property_cell->value(),
                                         broker()->isolate());

      if ((!property_details.IsConfigurable() &&
           property_details.IsReadOnly()) ||
          property_details.cell_type() == PropertyCellType::kConstant ||
          property_details.cell_type() == PropertyCellType::kUndefined) {
        return property_cell_value;
      }
    }
  }

  return MaybeHandle<Object>();
}

void SerializerForBackgroundCompilation::VisitBytecode(
    SourcePositionTableIterator* iterator) {
  // TODO(mslekova): Abstract away and call UpdateSourcePosition

  switch (bytecode_iterator().current_bytecode()) {
#define DEFINE_BYTECODE_CASE(name)     \
  case interpreter::Bytecode::k##name: \
    Visit##name();                     \
    break;
    SUPPORTED_BYTECODE_LIST(DEFINE_BYTECODE_CASE)
#undef DEFINE_BYTECODE_CASE
    default: {
      // printf("Visiting bytecode: %u\n",
      //        unsigned(bytecode_iterator().current_bytecode()));
      break;
    }
  }
}

void SerializerForBackgroundCompilation::VisitLdaGlobal() {
  Handle<Name> name(
      Name::cast(bytecode_iterator().GetConstantForIndexOperand(0)),
      broker()->isolate());

  // TODO(mslekova): cache these
  Handle<NativeContext> native_context = broker()->native_context().object();
  Handle<JSGlobalObject> global_object =
      handle(native_context->global_object(), broker()->isolate());
  Handle<ScriptContextTable> table =
      handle(native_context->script_context_table(), broker()->isolate());

  ScriptContextTable::LookupResult result;
  if (ScriptContextTable::Lookup(broker()->isolate(), table,
                                 Handle<String>::cast(name), &result)) {
    Handle<Context> script_context = ScriptContextTable::GetContext(
        broker()->isolate(), table, result.context_index);

    Object* contents = script_context->get(result.slot_index);
    environment()->AddSeenAccumulatorValue(
        handle(contents, broker()->isolate()));
  } else {
    LookupIterator it(broker()->isolate(), global_object, name,
                      LookupIterator::OWN);
    MaybeHandle<Object> maybe_value = GetConstantFromLookup(it);
    if (!maybe_value.is_null()) {
      environment()->AddSeenAccumulatorValue(maybe_value.ToHandleChecked());
    }
  }
}

void SerializerForBackgroundCompilation::VisitLdaUndefined() {
  environment()->AddSeenAccumulatorValue(
      broker()->isolate()->factory()->undefined_value());
}

void SerializerForBackgroundCompilation::VisitLdaZero() {
  Handle<Object> zero = handle(Smi::FromInt(0), broker()->isolate());
  environment()->AddSeenAccumulatorValue(zero);
}

void SerializerForBackgroundCompilation::VisitLdaSmi() {
  Handle<Object> smi =
      handle(Smi::FromInt(bytecode_iterator().GetImmediateOperand(0)),
             broker()->isolate());
  environment()->AddSeenAccumulatorValue(smi);
}

void SerializerForBackgroundCompilation::VisitLdaConstant() {
  Handle<Object> constant = handle(
      bytecode_iterator().GetConstantForIndexOperand(0), broker()->isolate());
  environment()->AddSeenAccumulatorValue(constant);
}

void SerializerForBackgroundCompilation::VisitLdaNamedProperty() {
  InferredState object_state =
      environment()->LookupRegister(bytecode_iterator().GetRegisterOperand(0));
  Handle<Name> name(
      Name::cast(bytecode_iterator().GetConstantForIndexOperand(1)),
      broker()->isolate());

  switch (object_state.kind) {
    case InferredState::Kind::kValuesAndMaps: {
      for (auto value : object_state.values) {
        if (value->IsNullOrUndefined()) continue;

        LookupIterator it = LookupIterator(broker()->isolate(), value, name);
        MaybeHandle<Object> maybe_value = GetConstantFromLookup(it, true);
        if (!maybe_value.is_null()) {
          environment()->AddSeenAccumulatorValue(maybe_value.ToHandleChecked());
        }
      }
      break;
    }
    case InferredState::Kind::kMaps: {
      for (auto map : object_state.maps) {
        USE(map);
      }
      break;
    }
    default: {
      break;
    }
  }
}

void SerializerForBackgroundCompilation::VisitLdaNamedPropertyNoFeedback() {
  VisitLdaNamedProperty();
}

void SerializerForBackgroundCompilation::VisitLdar() {
  // InferredState state = environment()->LookupRegister(
  //   bytecode_iterator().GetRegisterOperand(0));
  // environment()->SetAccumulatorInferredState(state);
}

void SerializerForBackgroundCompilation::VisitStar() {
  InferredState state = environment()->LookupAccumulator();
  environment()->SetRegisterInferredState(
      bytecode_iterator().GetRegisterOperand(0), state);
}

void SerializerForBackgroundCompilation::VisitMov() {
  InferredState state =
      environment()->LookupRegister(bytecode_iterator().GetRegisterOperand(0));
  environment()->SetRegisterInferredState(
      bytecode_iterator().GetRegisterOperand(1), state);
}

void SerializerForBackgroundCompilation::VisitStaNamedProperty() {
  InferredState value_state = environment()->LookupAccumulator();
  InferredState object_state =
      environment()->LookupRegister(bytecode_iterator().GetRegisterOperand(0));
  Handle<Name> name(
      Name::cast(bytecode_iterator().GetConstantForIndexOperand(1)),
      broker()->isolate());

  switch (value_state.kind) {
    case InferredState::Kind::kValuesAndMaps: {
      for (auto value : value_state.values) {
        environment()->AddSeenRegisterValue(
            bytecode_iterator().GetRegisterOperand(0), value);
      }
      break;
    }
    default: {
      break;
    }
  }
}

void SerializerForBackgroundCompilation::VisitStaNamedPropertyNoFeedback() {
  VisitStaNamedProperty();
}

void SerializerForBackgroundCompilation::VisitStaNamedOwnProperty() {
  VisitStaNamedProperty();
}

void SerializerForBackgroundCompilation::VisitCallUndefinedReceiver0() {
  InferredState receiver_state(local_zone());
  // TODO(mslekova): Think if we should encapsulate these as methods
  // in the InferredState class.
  receiver_state.kind = InferredState::Kind::kValuesAndMaps;
  receiver_state.values.push_back(
      broker()->isolate()->factory()->undefined_value());

  InferredState callee_state =
      environment()->LookupRegister(bytecode_iterator().GetRegisterOperand(0));

  InferredStateVector parameters_state(local_zone());

  Environment* env = Environment::NewFromCall(
      this, broker()->isolate(), bytecode_iterator_, closure_, callee_state,
      receiver_state, parameters_state);

  // TODO(mslekova): Call set_environment(env);
  // Traverse recursively.
  USE(env);
}

void SerializerForBackgroundCompilation::VisitCallUndefinedReceiver1() {
  InferredState receiver_state(local_zone());
  // TODO(mslekova): Think if we should encapsulate these as methods
  // in the InferredState class.
  receiver_state.kind = InferredState::Kind::kValuesAndMaps;
  receiver_state.values.push_back(
      broker()->isolate()->factory()->undefined_value());

  InferredState callee_state =
      environment()->LookupRegister(bytecode_iterator().GetRegisterOperand(0));

  InferredState arg0 =
      environment()->LookupRegister(bytecode_iterator().GetRegisterOperand(1));

  InferredStateVector parameters_state(local_zone());
  parameters_state.push_back(arg0);

  Environment* env = Environment::NewFromCall(
      this, broker()->isolate(), bytecode_iterator_, closure_, callee_state,
      receiver_state, parameters_state);

  // TODO(mslekova): Call set_environment(env);
  // Traverse recursively.
  USE(env);
}

void SerializerForBackgroundCompilation::VisitCallProperty0() {
  InferredState callee_state =
      environment()->LookupRegister(bytecode_iterator().GetRegisterOperand(0));
  InferredState receiver_state =
      environment()->LookupRegister(bytecode_iterator().GetRegisterOperand(1));

  InferredStateVector parameters_state(local_zone());

  Environment* env = Environment::NewFromCall(
      this, broker()->isolate(), bytecode_iterator_, closure_, callee_state,
      receiver_state, parameters_state);

  // TODO(mslekova): Call set_environment(env);
  // Traverse recursively.
  USE(env);
}

void SerializerForBackgroundCompilation::VisitCallProperty1() {
  InferredState callee_state =
      environment()->LookupRegister(bytecode_iterator().GetRegisterOperand(0));
  InferredState receiver_state =
      environment()->LookupRegister(bytecode_iterator().GetRegisterOperand(1));
  InferredState arg0 =
      environment()->LookupRegister(bytecode_iterator().GetRegisterOperand(2));

  InferredStateVector parameters_state(local_zone());
  parameters_state.push_back(arg0);

  Environment* env = Environment::NewFromCall(
      this, broker()->isolate(), bytecode_iterator_, closure_, callee_state,
      receiver_state, parameters_state);

  // TODO(mslekova): Call set_environment(env);
  // Traverse recursively.
  USE(env);
}

void SerializerForBackgroundCompilation::VisitCallProperty2() {
  InferredState callee_state =
      environment()->LookupRegister(bytecode_iterator().GetRegisterOperand(0));
  InferredState receiver_state =
      environment()->LookupRegister(bytecode_iterator().GetRegisterOperand(1));
  InferredState arg0 =
      environment()->LookupRegister(bytecode_iterator().GetRegisterOperand(2));
  InferredState arg1 =
      environment()->LookupRegister(bytecode_iterator().GetRegisterOperand(3));

  InferredStateVector parameters_state(local_zone());
  parameters_state.push_back(arg0);
  parameters_state.push_back(arg1);

  Environment* env = Environment::NewFromCall(
      this, broker()->isolate(), bytecode_iterator_, closure_, callee_state,
      receiver_state, parameters_state);

  // TODO(mslekova): Call set_environment(env);
  // Traverse recursively.
  USE(env);
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
