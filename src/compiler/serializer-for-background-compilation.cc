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
  explicit Environment(SerializerForBackgroundCompilation* serializer);

 private:
  Zone* zone() const { return serializer_->local_zone(); }

  SerializerForBackgroundCompilation* serializer_;
};

SerializerForBackgroundCompilation::Environment::Environment(
    SerializerForBackgroundCompilation* serializer)
    : serializer_(serializer) {}

SerializerForBackgroundCompilation::SerializerForBackgroundCompilation(
    JSHeapBroker* broker, Zone* local_zone, Handle<SharedFunctionInfo> info,
    Handle<FeedbackVector> feedback, Handle<JSFunction> closure)
    : broker_(broker),
      local_zone_(local_zone),
      environment_(nullptr),
      bytecode_array_(handle(info->GetBytecodeArray(), broker->isolate())) {}

void SerializerForBackgroundCompilation::Run() {
  Environment env(this);
  set_environment(&env);

  TraverseBytecode();
}

void SerializerForBackgroundCompilation::TraverseBytecode() {
  interpreter::BytecodeArrayIterator iterator(bytecode_array());
  set_bytecode_iterator(&iterator);

  SourcePositionTableIterator source_position_iterator(
      handle(bytecode_array()->SourcePositionTable(), broker()->isolate()));

  // TODO(mslekova): Handle osr

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
