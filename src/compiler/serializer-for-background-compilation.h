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
class Register;
}  // namespace interpreter

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
  V(JumpIfJSReceiver)             \
  V(Throw)                        \
  V(Abort)                        \
  V(ReThrow)                      \
  V(Return)                       \
  V(CallRuntime)                  \
  V(CallRuntimeForPair)           \
  V(StaContextSlot)               \
  V(StaCurrentContextSlot)        \
  V(CreateBlockContext)           \
  V(CreateFunctionContext)        \
  V(CreateEvalContext)            \
  V(PushContext)                  \
  V(PopContext)

#define CLEAR_ACCUMULATOR_LIST(V)   \
  V(Construct)                      \
  V(CreateClosure)                  \
  V(CreateEmptyObjectLiteral)       \
  V(LdaContextSlot)                 \
  V(LdaImmutableContextSlot)        \
  V(LdaCurrentContextSlot)          \
  V(LdaImmutableCurrentContextSlot) \
  V(LdaKeyedProperty)               \
  V(CreateMappedArguments)          \
  V(CreateUnmappedArguments)        \
  V(CreateRestParameter)

#define SUPPORTED_BYTECODE_LIST(V) \
  V(Illegal)                       \
  V(Wide)                          \
  V(ExtraWide)                     \
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
  V(CallUndefinedReceiver)         \
  V(CallUndefinedReceiver0)        \
  V(CallUndefinedReceiver1)        \
  V(CallUndefinedReceiver2)        \
  V(CallAnyReceiver)               \
  V(CallNoFeedback)                \
  V(CallProperty)                  \
  V(CallProperty0)                 \
  V(CallProperty1)                 \
  V(CallProperty2)                 \
  V(JumpLoop)                      \
  SKIPPED_JUMPS_LIST(V)            \
  CLEAR_ACCUMULATOR_LIST(V)

class JSHeapBroker;
struct InferredState;
typedef ZoneVector<InferredState> InferredStateVector;

// The SerializerForBackgroundCompilation makes sure that the relevant function
// data such as bytecode, SharedFunctionInfo and FeedbackVector, used by later
// optimizations in the compiler, is copied to the heap broker.
class SerializerForBackgroundCompilation {
 public:
  class Environment;

  explicit SerializerForBackgroundCompilation(
      JSHeapBroker* broker, Zone* local_zone, Handle<JSFunction> closure,
      Environment* environment = nullptr);

  void Run();

  Zone* zone() const { return zone_; }

  const Handle<BytecodeArray>& bytecode_array() const {
    return bytecode_array_;
  }

 private:
  void TraverseBytecode();

#define DECLARE_VISIT_BYTECODE(name, ...) \
  void Visit##name(interpreter::BytecodeArrayIterator* iterator);
  SUPPORTED_BYTECODE_LIST(DECLARE_VISIT_BYTECODE)
#undef DECLARE_VISIT_BYTECODE

  JSHeapBroker* broker() const { return broker_; }
  Environment* environment() const { return environment_; }

  MaybeHandle<Object> GetConstantFromLookup(LookupIterator& iterator,
                                            bool named = false) const;

  void ProcessCall(const InferredState& callee_state,
                   const InferredState& receiver_state,
                   const InferredStateVector& parameters_state, int arg_count);
  void ProcessCallArguments(InferredState& callee_state,
                            interpreter::Register receiver, size_t reg_count,
                            InferredStateVector& args);
  void ProcessCallVarArgs(interpreter::BytecodeArrayIterator* iterator,
                          ConvertReceiverMode receiver_mode);

  void GetCallArgumentsFromRegisters(InferredState& callee_state,
                                     InferredState& receiver_state,
                                     interpreter::Register first_arg,
                                     int arg_count, InferredStateVector& args);

  JSHeapBroker* broker_;
  Zone* zone_;
  Environment* environment_;

  Handle<JSFunction> closure_;
  Handle<BytecodeArray> bytecode_array_;

  // Cached members for global loads/stores
  Handle<NativeContext> native_context_;
  Handle<ScriptContextTable> script_context_table_;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_SERIALIZER_FOR_BACKGROUND_COMPILATION_H_
