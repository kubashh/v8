// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_SERIALIZER_FOR_BACKGROUND_COMPILATION_H_
#define V8_COMPILER_SERIALIZER_FOR_BACKGROUND_COMPILATION_H_

#include "src/handles.h"
#include "src/zone/zone-containers.h"

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
class NativeContext;
class ScriptContextTable;

namespace compiler {

#define SKIPPED_JUMPS_LIST(V)     \
  V(Jump)                         \
  V(JumpConstant)                 \
  V(JumpIfNullConstant)           \
  V(JumpIfNotNullConstant)        \
  V(JumpIfUndefinedConstant)      \
  V(JumpIfNotUndefinedConstant)   \
  V(JumpIfTrueConstant)           \
  V(JumpIfFalseConstant)          \
  V(JumpIfJSReceiverConstant)     \
  V(JumpIfToBooleanTrueConstant)  \
  V(JumpIfToBooleanFalseConstant) \
  V(JumpIfToBooleanTrue)          \
  V(JumpIfToBooleanFalse)         \
  V(JumpIfTrue)                   \
  V(JumpIfFalse)                  \
  V(JumpIfNull)                   \
  V(JumpIfNotNull)                \
  V(JumpIfUndefined)              \
  V(JumpIfNotUndefined)           \
  V(JumpIfJSReceiver)

#define SUPPORTED_BYTECODE_LIST(V) \
  V(LdaGlobal)                     \
  V(LdaGlobalInsideTypeof)         \
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
  V(CallAnyReceiver)               \
  V(CallProperty)                  \
  V(CallProperty0)                 \
  V(CallProperty1)                 \
  V(CallProperty2)                 \
  V(CreateEmptyObjectLiteral)      \
  V(JumpLoop)                      \
  SKIPPED_JUMPS_LIST(V)

class JSHeapBroker;
struct InferredState;
typedef ZoneVector<InferredState> InferredStateVector;

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

  void ProcessCall(const InferredState& callee_state,
                   const InferredState& receiver_state,
                   const InferredStateVector& parameters_state);
  void ProcessCallVarArgs(ConvertReceiverMode receiver_mode);

  JSHeapBroker* broker_;
  Zone* local_zone_;
  Environment* environment_;

  Handle<JSFunction> closure_;
  Handle<BytecodeArray> bytecode_array_;
  const interpreter::BytecodeArrayIterator* bytecode_iterator_;

  // Cached members for global loads/stores
  Handle<NativeContext> native_context_;
  Handle<ScriptContextTable> script_context_table_;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_SERIALIZER_FOR_BACKGROUND_COMPILATION_H_
