// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_SERIALIZER_FOR_BACKGROUND_COMPILATION_H_
#define V8_COMPILER_SERIALIZER_FOR_BACKGROUND_COMPILATION_H_

#include "src/handles.h"

namespace v8 {
namespace internal {

namespace interpreter {
class BytecodeArrayIterator;
}

class FeedbackVector;
class SharedFunctionInfo;
class BytecodeArray;
class SourcePositionTableIterator;
class Zone;

namespace compiler {

#define SUPPORTED_BYTECODE_LIST(V) \
  V(LdaGlobal)                     \
  V(LdaUndefined)                  \
  V(LdaNull)                       \
  V(LdaZero)                       \
  V(LdaSmi)                        \
  V(LdaConstant)                   \
  V(LdaNamedProperty)              \
  V(LdaNamedPropertyNoFeedback)    \
  V(Ldar)                          \
  V(Star)                          \
  V(Mov)                           \
  V(StaNamedProperty)              \
  V(StaNamedPropertyNoFeedback)    \
  V(StaNamedOwnProperty)           \
  V(CallUndefinedReceiver0)        \
  V(CallUndefinedReceiver1)        \
  V(CallProperty0)                 \
  V(CallProperty1)                 \
  V(CallProperty2)

class JSHeapBroker;

// The SerializerForBackgroundCompilation makes sure that the relevant function
// data such as bytecode, SharedFunctionInfo and FeedbackVector, used by later
// optimizations in the compiler, is copied to the heap broker.
class SerializerForBackgroundCompilation {
 public:
  explicit SerializerForBackgroundCompilation(JSHeapBroker* broker,
                                              Zone* local_zone,
                                              Handle<JSFunction> closure);

  void Run();

  Zone* local_zone() const { return local_zone_; }

  const Handle<BytecodeArray>& bytecode_array() const {
    return bytecode_array_;
  }

  // TODO(mslekova): Use pointer instead of a ref.
  const interpreter::BytecodeArrayIterator& bytecode_iterator() const {
    return *bytecode_iterator_;
  }
  void set_bytecode_iterator(
      interpreter::BytecodeArrayIterator* bytecode_iterator) {
    bytecode_iterator_ = bytecode_iterator;
  }

 private:
  class Environment;

  void TraverseBytecode();
  void VisitBytecode(SourcePositionTableIterator* iterator);

#define DECLARE_VISIT_BYTECODE(name, ...) void Visit##name();
  SUPPORTED_BYTECODE_LIST(DECLARE_VISIT_BYTECODE)
#undef DECLARE_VISIT_BYTECODE

  JSHeapBroker* broker() const { return broker_; }
  void set_environment(Environment* env) { environment_ = env; }
  Environment* environment() const { return environment_; }

  MaybeHandle<Object> GetConstantFromLookup(LookupIterator& iterator,
                                            bool named = false) const;
  MaybeHandle<Object> SetValueOnObject(Handle<Object> object, Handle<Name> name,
                                       Handle<Object> value);

  JSHeapBroker* broker_;
  Zone* local_zone_;
  Environment* environment_;

  Handle<JSFunction> closure_;
  Handle<BytecodeArray> bytecode_array_;
  const interpreter::BytecodeArrayIterator* bytecode_iterator_;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_SERIALIZER_FOR_BACKGROUND_COMPILATION_H_
