// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/serializer-for-background-compilation.h"

#include "src/compiler/js-heap-broker.h"
// #include "src/feedback-vector.h"
#include "src/handles-inl.h"
#include "src/interpreter/bytecode-array-iterator.h"
#include "src/objects/code.h"
#include "src/objects/shared-function-info-inl.h"
// #include "src/source-position-table.h"
// #include "src/vector-slot-pair.h"
#include "src/zone/zone.h"

namespace v8 {
namespace internal {
namespace compiler {

class SerializerForBackgroundCompilation::Environment : public ZoneObject {
 public:
  explicit Environment(SerializerForBackgroundCompilation* serializer,
                       Isolate* isolate, int register_count,
                       int parameter_count,
                       const interpreter::BytecodeArrayIterator* iterator,
                       Handle<JSFunction> closure);

  typedef ZoneVector<Handle<Map>> MapVector;
  typedef ZoneVector<Handle<Object>> ValueVector;
  struct InferredState {
    enum class Kind { kNone, kMaps, kValuesAndMaps };

    static InferredState EmptyState(Zone* zone) {
      InferredState ret = {Kind::kNone, MapVector(zone), ValueVector(zone)};
      return ret;
    }

    Kind kind;
    MapVector inferred_maps;
    ValueVector inferred_values;
  };

 private:
  Zone* zone() const { return serializer_->local_zone(); }

  SerializerForBackgroundCompilation* serializer_;

  // Contains the currently known state of the registers, the accumulator
  // and the parameters. The structure is inspired by
  // BytecodeGraphBuilder::Environment and looks like:
  // values: receiver | parameters | registers | accumulator
  // indices:    0         register_base_ accumulator_base_
  ZoneVector<InferredState> inferred_state;
  int register_base_;
  int accumulator_base_;

  Handle<SharedFunctionInfo> shared_function_info_;
  Handle<FeedbackVector> feedback_vector_;
};

SerializerForBackgroundCompilation::Environment::Environment(
    SerializerForBackgroundCompilation* serializer, Isolate* isolate,
    int register_count, int parameter_count,
    const interpreter::BytecodeArrayIterator* iterator,
    Handle<JSFunction> closure)
    : serializer_(serializer),
      inferred_state(register_count + parameter_count + 1,
                     InferredState::EmptyState(serializer->local_zone()),
                     serializer->local_zone()),
      shared_function_info_(handle(closure->shared(), isolate)),
      feedback_vector_(handle(closure->feedback_vector(), isolate)) {
  // Parameters including the receiver

  // Registers
  register_base_ = parameter_count;

  // Accumulator
  accumulator_base_ = register_base_ + register_count;

  // Context
  // TODO(mslekova): DO we need context_index?
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

void SerializerForBackgroundCompilation::VisitBytecode(
    SourcePositionTableIterator* iterator) {
  // TODO(mslekova): Abstract away and call UpdateSourcePosition

  switch (bytecode_iterator().current_bytecode()) {
    default:
      printf("Visiting bytecode: %u\n",
             unsigned(bytecode_iterator().current_bytecode()));
      break;
  }
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
