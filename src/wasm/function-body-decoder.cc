// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/signature.h"

#include "src/base/platform/elapsed-timer.h"
#include "src/flags.h"
#include "src/handles.h"
#include "src/objects-inl.h"
#include "src/zone/zone-containers.h"

#include "src/wasm/decoder.h"
#include "src/wasm/function-body-decoder-impl.h"
#include "src/wasm/function-body-decoder.h"
#include "src/wasm/wasm-limits.h"
#include "src/wasm/wasm-module.h"
#include "src/wasm/wasm-opcodes.h"

#include "src/ostreams.h"

#include "src/compiler/wasm-compiler.h"

namespace v8 {
namespace internal {
namespace wasm {

namespace {

// An SsaEnv environment carries the current local variable renaming
// as well as the current effect and control dependency in the TF graph.
// It maintains a control state that tracks whether the environment
// is reachable, has reached a control end, or has been merged.
struct SsaEnv {
  enum State { kControlEnd, kUnreachable, kReached, kMerged };

  State state;
  TFNode* control;
  TFNode* effect;
  TFNode** locals;

  bool go() { return state >= kReached; }
  void Kill(State new_state = kControlEnd) {
    state = new_state;
    locals = nullptr;
    control = nullptr;
    effect = nullptr;
  }
  void SetNotMerged() {
    if (state == kMerged) state = kReached;
  }
};

// Macros that build nodes only if there is a graph and the current SSA
// environment is reachable from start. This avoids problems with malformed
// TF graphs when decoding inputs that have unreachable code.
#define BUILD(func, ...)                                                    \
  (build(decoder) ? CheckForException(decoder, builder_->func(__VA_ARGS__)) \
                  : nullptr)

constexpr uint32_t kNullCatch = static_cast<uint32_t>(-1);

class WasmGraphBuildingConsumer : public ConsumerTemplate {
 public:
  struct CValue {
    TFNode* node;

    static CValue Unreachable() { return {nullptr}; }
    static CValue New() { return {nullptr}; }
  };

  struct TryInfo : public ZoneObject {
    SsaEnv* catch_env;
    TFNode* exception;

    explicit TryInfo(SsaEnv* c) : catch_env(c), exception(nullptr) {}
  };

  struct CControl {
    SsaEnv* end_env;         // end environment for the construct.
    SsaEnv* false_env;       // false environment (only for if).
    TryInfo* try_info;       // information used for compiling try statements.
    int32_t previous_catch;  // previous Control (on the stack) with a catch.

    static CControl Block() { return {}; }
    static CControl If() { return {}; }
    static CControl Loop() { return {}; }
    static CControl Try() { return {}; }
  };

  explicit WasmGraphBuildingConsumer(TFBuilder* builder) : builder_(builder) {}

  IMPL(StartFunction) {
    SsaEnv* ssa_env =
        reinterpret_cast<SsaEnv*>(decoder->zone()->New(sizeof(SsaEnv)));
    uint32_t env_count = decoder->NumLocals();
    size_t size = sizeof(TFNode*) * env_count;
    ssa_env->state = SsaEnv::kReached;
    ssa_env->locals =
        size > 0 ? reinterpret_cast<TFNode**>(decoder->zone()->New(size))
                 : nullptr;

    TFNode* start =
        builder_->Start(static_cast<int>(decoder->sig_->parameter_count() + 1));
    // Initialize local variables.
    uint32_t index = 0;
    for (; index < decoder->sig_->parameter_count(); ++index) {
      ssa_env->locals[index] = builder_->Param(index);
    }
    while (index < env_count) {
      ValueType type = decoder->GetLocalType(index);
      TFNode* node = DefaultValue(type);
      while (index < env_count && decoder->GetLocalType(index) == type) {
        // Do a whole run of like-typed locals at a time.
        ssa_env->locals[index++] = node;
      }
    }
    ssa_env->control = start;
    ssa_env->effect = start;
    SetEnv(ssa_env);
  }

  IMPL(FinishFunction) { builder_->PatchInStackCheckIfNeeded(); }

  IMPL(StartFunctionBody, Control<Consumer>* block) {
    SsaEnv* break_env = ssa_env_;
    SetEnv(Steal(decoder->zone(), break_env));
    block->consumer_data.end_env = break_env;
  }

  IMPL(Unreachable) { BUILD(Unreachable, decoder->position()); }

  IMPL(FallThruTo, Control<Consumer>* c) {
    MergeValuesInto(decoder, c);
    SetEnv(c->consumer_data.end_env);
  }

  IMPL(I32Const, Value<Consumer>* result, int32_t value) {
    result->consumer_data.node = builder_->Int32Constant(value);
  }

  IMPL(I64Const, Value<Consumer>* result, int64_t value) {
    result->consumer_data.node = builder_->Int64Constant(value);
  }

  IMPL(F32Const, Value<Consumer>* result, float value) {
    result->consumer_data.node = builder_->Float32Constant(value);
  }

  IMPL(F64Const, Value<Consumer>* result, double value) {
    result->consumer_data.node = builder_->Float64Constant(value);
  }

  IMPL(GetLocal, Value<Consumer>* result,
       const LocalIndexOperand<do_validation>& operand) {
    if (!ssa_env_->locals) return;  // unreachable
    result->consumer_data.node = ssa_env_->locals[operand.index];
  }

  IMPL(SetLocal, const Value<Consumer>& value,
       const LocalIndexOperand<do_validation>& operand) {
    if (!ssa_env_->locals) return;  // unreachable
    ssa_env_->locals[operand.index] = value.consumer_data.node;
  }

  IMPL(TeeLocal, const Value<Consumer>& value, Value<Consumer>* result,
       const LocalIndexOperand<do_validation>& operand) {
    result->consumer_data.node = value.consumer_data.node;
    if (!ssa_env_->locals) return;  // unreachable
    ssa_env_->locals[operand.index] = value.consumer_data.node;
  }

  IMPL(GetGlobal, Value<Consumer>* result,
       const GlobalIndexOperand<do_validation>& operand) {
    result->consumer_data.node = BUILD(GetGlobal, operand.index);
  }

  IMPL(SetGlobal, const Value<Consumer>& value,
       const GlobalIndexOperand<do_validation>& operand) {
    BUILD(SetGlobal, operand.index, value.consumer_data.node);
  }

  IMPL(UnOp, WasmOpcode opcode, FunctionSig* sig, const Value<Consumer>& value,
       Value<Consumer>* result) {
    result->consumer_data.node =
        BUILD(Unop, opcode, value.consumer_data.node, decoder->position());
  }

  IMPL(BinOp, WasmOpcode opcode, FunctionSig* sig, const Value<Consumer>& lhs,
       const Value<Consumer>& rhs, Value<Consumer>* result) {
    result->consumer_data.node =
        BUILD(Binop, opcode, lhs.consumer_data.node, rhs.consumer_data.node,
              decoder->position());
  }

  IMPL(DoReturn, Vector<Value<Consumer>> values) {
    size_t num_values = values.size();
    TFNode** buffer = GetNodes(values);
    for (size_t i = 0; i < num_values; ++i) {
      buffer[i] = values[i].consumer_data.node;
    }
    BUILD(Return, static_cast<unsigned>(values.size()), buffer);
  }

  IMPL(If, const Value<Consumer>& cond, Control<Consumer>* if_block) {
    TFNode* if_true = nullptr;
    TFNode* if_false = nullptr;
    BUILD(BranchNoHint, cond.consumer_data.node, &if_true, &if_false);
    SsaEnv* end_env = ssa_env_;
    SsaEnv* false_env = Split(decoder, ssa_env_);
    false_env->control = if_false;
    SsaEnv* true_env = Steal(decoder->zone(), ssa_env_);
    true_env->control = if_true;
    if_block->consumer_data.end_env = end_env;
    if_block->consumer_data.false_env = false_env;
    SetEnv(true_env);
  }

  IMPL(Else, Control<Consumer>* if_block) {
    SetEnv(if_block->consumer_data.false_env);
  }

  IMPL(BreakTo, Control<Consumer>* block) {
    if (block->is_loop()) {
      Goto(decoder, ssa_env_, block->consumer_data.end_env);
    } else {
      MergeValuesInto(decoder, block);
    }
  }

  IMPL(PopControl, const Control<Consumer>& block) {
    if (block.is_onearmed_if()) {
      Goto(decoder, block.consumer_data.false_env, block.consumer_data.end_env);
    } else if (block.is_try_catch()) {
      SsaEnv* fallthru_ssa_env = ssa_env_;
      DCHECK_NOT_NULL(block.consumer_data.try_info->catch_env);
      SetEnv(block.consumer_data.try_info->catch_env);
      BUILD(Rethrow);
      // TODO(karlschimpf): Why not use EndControl ()? (currently fails)
      // FallThruTo(decoder, &block);
      SetEnv(fallthru_ssa_env);
    }
  }

  IMPL(BrIf, const Value<Consumer>& cond, Control<Consumer>* block) {
    SsaEnv* fenv = ssa_env_;
    SsaEnv* tenv = Split(decoder, fenv);
    fenv->SetNotMerged();
    BUILD(BranchNoHint, cond.consumer_data.node, &tenv->control,
          &fenv->control);
    ssa_env_ = tenv;
    BreakTo(decoder, block);
    ssa_env_ = fenv;
  }

  IMPL(EndControl, Control<Consumer>* block) { ssa_env_->Kill(); }

  IMPL(Block, Control<Consumer>* block) {
    // The break environment is the outer environment.
    block->consumer_data.end_env = ssa_env_;
    SetEnv(Steal(decoder->zone(), ssa_env_));
  }

  IMPL(Loop, Control<Consumer>* block) {
    SsaEnv* finish_try_env = Steal(decoder->zone(), ssa_env_);
    block->consumer_data.end_env = finish_try_env;
    // The continue environment is the inner environment.
    SetEnv(PrepareForLoop(decoder, finish_try_env));
    ssa_env_->SetNotMerged();
  }

  IMPL(LoadMem, ValueType type, MachineType mem_type,
       const MemoryAccessOperand<do_validation>& operand,
       const Value<Consumer>& index, Value<Consumer>* result) {
    result->consumer_data.node =
        BUILD(LoadMem, type, mem_type, index.consumer_data.node, operand.offset,
              operand.alignment, decoder->position());
  }

  IMPL(StoreMem, ValueType type, MachineType mem_type,
       const MemoryAccessOperand<do_validation>& operand,
       const Value<Consumer>& index, const Value<Consumer>& value) {
    BUILD(StoreMem, mem_type, index.consumer_data.node, operand.offset,
          operand.alignment, value.consumer_data.node, decoder->position());
  }

  IMPL(GrowMemory, const Value<Consumer>& value, Value<Consumer>* result) {
    result->consumer_data.node = BUILD(GrowMemory, value.consumer_data.node);
  }

  IMPL(CurrentMemoryPages, Value<Consumer>* result) {
    result->consumer_data.node = BUILD(CurrentMemoryPages);
  }

  IMPL(CallDirect, const CallFunctionOperand<do_validation>& operand,
       const Value<Consumer> args[], Value<Consumer> returns[]) {
    DoCall(decoder, nullptr, operand, args, returns, false);
  }

  IMPL(CallIndirect, const Value<Consumer>& index,
       const CallIndirectOperand<do_validation>& operand,
       const Value<Consumer> args[], Value<Consumer> returns[]) {
    DoCall(decoder, index.consumer_data.node, operand, args, returns, true);
  }

  IMPL(SimdLaneOp, WasmOpcode opcode,
       const SimdLaneOperand<do_validation> operand,
       Vector<Value<Consumer>> inputs, Value<Consumer>* result) {
    TFNode** nodes = GetNodes(inputs);
    result->consumer_data.node = BUILD(SimdLaneOp, opcode, operand.lane, nodes);
  }

  IMPL(SimdShiftOp, WasmOpcode opcode,
       const SimdShiftOperand<do_validation> operand,
       const Value<Consumer>& input, Value<Consumer>* result) {
    TFNode* inputs[] = {input.consumer_data.node};
    result->consumer_data.node =
        BUILD(SimdShiftOp, opcode, operand.shift, inputs);
  }

  IMPL(Simd8x16ShuffleOp, const Simd8x16ShuffleOperand<do_validation>& operand,
       const Value<Consumer>& input0, const Value<Consumer>& input1,
       Value<Consumer>* result) {
    TFNode* input_nodes[] = {input0.consumer_data.node,
                             input1.consumer_data.node};
    result->consumer_data.node =
        BUILD(Simd8x16ShuffleOp, operand.shuffle, input_nodes);
  }

  IMPL(SimdOp, WasmOpcode opcode, Vector<Value<Consumer>> args,
       Value<Consumer>* result) {
    TFNode** inputs = GetNodes(args);
    TFNode* node = BUILD(SimdOp, opcode, inputs);
    if (result) result->consumer_data.node = node;
  }

  IMPL(AtomicOp, WasmOpcode opcode, Vector<Value<Consumer>> args,
       Value<Consumer>* result) {
    TFNode** inputs = GetNodes(args);
    TFNode* node = BUILD(AtomicOp, opcode, inputs, decoder->position());
    if (result) result->consumer_data.node = node;
  }

  IMPL(BrTable, const BranchTableOperand<do_validation>& operand,
       const Value<Consumer>& key) {
    SsaEnv* break_env = ssa_env_;
    // Build branches to the various blocks based on the table.
    TFNode* sw = BUILD(Switch, operand.table_count + 1, key.consumer_data.node);

    SsaEnv* copy = Steal(decoder->zone(), break_env);
    ssa_env_ = copy;
    BranchTableIterator<do_validation> iterator(decoder, operand);
    while (iterator.has_next()) {
      uint32_t i = iterator.cur_index();
      uint32_t target = iterator.next();
      ssa_env_ = Split(decoder, copy);
      ssa_env_->control = (i == operand.table_count) ? BUILD(IfDefault, sw)
                                                     : BUILD(IfValue, i, sw);
      BreakTo(decoder, decoder->control_at(target));
    }
    DCHECK(decoder->ok());
    ssa_env_ = break_env;
  }

  IMPL(Select, const Value<Consumer>& cond, const Value<Consumer>& fval,
       const Value<Consumer>& tval, Value<Consumer>* result) {
    TFNode* controls[2];
    BUILD(BranchNoHint, cond.consumer_data.node, &controls[0], &controls[1]);
    TFNode* merge = BUILD(Merge, 2, controls);
    TFNode* vals[2] = {tval.consumer_data.node, fval.consumer_data.node};
    TFNode* phi = BUILD(Phi, tval.type, 2, vals, merge);
    result->consumer_data.node = phi;
    ssa_env_->control = merge;
  }

  IMPL(Catch, const ExceptionIndexOperand<do_validation>& operand,
       Control<Consumer>* block) {
    DCHECK_NOT_NULL(block->consumer_data.try_info);
    current_catch_ = block->consumer_data.previous_catch;

    // Get the exception and see if wanted exception.
    TFNode* exception_as_i32 = BUILD(
        Catch, block->consumer_data.try_info->exception, decoder->position());
    TFNode* exception_tag = GetExceptionTag(decoder, operand);
    TFNode* compare_i32 = BUILD(Binop, kExprI32Eq, exception_as_i32,
                                exception_tag, decoder->position());
    TFNode* if_true = nullptr;
    TFNode* if_false = nullptr;
    BUILD(BranchNoHint, compare_i32, &if_true, &if_false);
    SsaEnv* end_env = ssa_env_;
    SsaEnv* false_env = Split(decoder, end_env);
    false_env->control = if_false;
    SsaEnv* true_env = Steal(decoder->zone(), ssa_env_);
    true_env->control = if_true;
    block->consumer_data.try_info->catch_env = false_env;
    SetEnv(true_env);
    // TODO(kschimpf): Add code to pop caught exception from isolate.
  }

  IMPL(Try, Control<Consumer>* block) {
    SsaEnv* outer_env = ssa_env_;
    SsaEnv* try_env = Steal(decoder->zone(), outer_env);
    SsaEnv* catch_env = UnreachableEnv(decoder->zone());
    SetEnv(try_env);
    TryInfo* try_info = new (decoder->zone()) TryInfo(catch_env);
    block->consumer_data.end_env = outer_env;
    block->consumer_data.try_info = try_info;
    block->consumer_data.previous_catch = current_catch_;
    current_catch_ = static_cast<int32_t>(decoder->control_depth() - 1);
  }

  IMPL_RET(TFNode*, GetExceptionTag,
           const ExceptionIndexOperand<do_validation>& operand) {
    // TODO(kschimpf): Need to get runtime exception tag values. This
    // code only handles non-imported/exported exceptions.
    return BUILD(Int32Constant, operand.index);
  }

  IMPL(Throw, const ExceptionIndexOperand<do_validation>& operand) {
    BUILD(Throw, GetExceptionTag(decoder, operand));
  }

 private:
  SsaEnv* ssa_env_;
  TFBuilder* builder_;
  uint32_t current_catch_ = kNullCatch;

  IMPL_RET(bool, build) {
    DCHECK_IMPLIES(!do_validation, decoder->ok());
    return ssa_env_->go() && (!do_validation || decoder->ok());
  }

  IMPL_RET(TryInfo*, current_try_info) {
    return decoder->control_at(decoder->control_depth() - 1 - current_catch_)
        ->consumer_data.try_info;
  }

  template <typename Consumer>
  TFNode** GetNodes(Value<Consumer>* values, size_t count) {
    TFNode** nodes = builder_->Buffer(count);
    for (size_t i = 0; i < count; ++i) {
      nodes[i] = values[i].consumer_data.node;
    }
    return nodes;
  }

  template <typename Consumer>
  TFNode** GetNodes(Vector<Value<Consumer>> values) {
    return GetNodes(values.start(), values.size());
  }

  void SetEnv(SsaEnv* env) {
#if DEBUG
    if (FLAG_trace_wasm_decoder) {
      char state = 'X';
      if (env) {
        switch (env->state) {
          case SsaEnv::kReached:
            state = 'R';
            break;
          case SsaEnv::kUnreachable:
            state = 'U';
            break;
          case SsaEnv::kMerged:
            state = 'M';
            break;
          case SsaEnv::kControlEnd:
            state = 'E';
            break;
        }
      }
      PrintF("{set_env = %p, state = %c", static_cast<void*>(env), state);
      if (env && env->control) {
        PrintF(", control = ");
        compiler::WasmGraphBuilder::PrintDebugName(env->control);
      }
      PrintF("}\n");
    }
#endif
    ssa_env_ = env;
    builder_->set_control_ptr(&env->control);
    builder_->set_effect_ptr(&env->effect);
  }

  IMPL_RET(TFNode*, CheckForException, TFNode* node) {
    if (node == nullptr) return nullptr;

    const bool inside_try_scope = current_catch_ != kNullCatch;

    if (!inside_try_scope) return node;

    TFNode* if_success = nullptr;
    TFNode* if_exception = nullptr;
    if (!builder_->ThrowsException(node, &if_success, &if_exception)) {
      return node;
    }

    SsaEnv* success_env = Steal(decoder->zone(), ssa_env_);
    success_env->control = if_success;

    SsaEnv* exception_env = Split(decoder, success_env);
    exception_env->control = if_exception;
    TryInfo* try_info = current_try_info(decoder);
    Goto(decoder, exception_env, try_info->catch_env);
    TFNode* exception = try_info->exception;
    if (exception == nullptr) {
      DCHECK_EQ(SsaEnv::kReached, try_info->catch_env->state);
      try_info->exception = if_exception;
    } else {
      DCHECK_EQ(SsaEnv::kMerged, try_info->catch_env->state);
      try_info->exception =
          CreateOrMergeIntoPhi(kWasmI32, try_info->catch_env->control,
                               try_info->exception, if_exception);
    }

    SetEnv(success_env);
    return node;
  }

  TFNode* DefaultValue(ValueType type) {
    switch (type) {
      case kWasmI32:
        return builder_->Int32Constant(0);
      case kWasmI64:
        return builder_->Int64Constant(0);
      case kWasmF32:
        return builder_->Float32Constant(0);
      case kWasmF64:
        return builder_->Float64Constant(0);
      case kWasmS128:
        return builder_->S128Zero();
      default:
        UNREACHABLE();
    }
  }

  IMPL(MergeValuesInto, Control<Consumer>* c) {
    if (!ssa_env_->go()) return;

    SsaEnv* target = c->consumer_data.end_env;
    const bool first = target->state == SsaEnv::kUnreachable;
    Goto(decoder, ssa_env_, target);

    size_t avail = decoder->stack_size() - decoder->control_at(0)->stack_depth;
    size_t start = avail >= c->merge.arity ? 0 : c->merge.arity - avail;
    for (size_t i = start; i < c->merge.arity; ++i) {
      auto& val = decoder->GetMergeValueFromStack(c, i);
      auto& old = c->merge[i];
      DCHECK_NOT_NULL(val.consumer_data.node);
      // TODO(clemensh): Remove first.
      DCHECK_EQ(first, old.consumer_data.node == nullptr);
      DCHECK(val.type == old.type || val.type == kWasmVar);
      old.consumer_data.node =
          first ? val.consumer_data.node
                : CreateOrMergeIntoPhi(old.type, target->control,
                                       old.consumer_data.node,
                                       val.consumer_data.node);
    }
  }

  IMPL(Goto, SsaEnv* from, SsaEnv* to) {
    DCHECK_NOT_NULL(to);
    if (!from->go()) return;
    switch (to->state) {
      case SsaEnv::kUnreachable: {  // Overwrite destination.
        to->state = SsaEnv::kReached;
        to->locals = from->locals;
        to->control = from->control;
        to->effect = from->effect;
        break;
      }
      case SsaEnv::kReached: {  // Create a new merge.
        to->state = SsaEnv::kMerged;
        // Merge control.
        TFNode* controls[] = {to->control, from->control};
        TFNode* merge = builder_->Merge(2, controls);
        to->control = merge;
        // Merge effects.
        if (from->effect != to->effect) {
          TFNode* effects[] = {to->effect, from->effect, merge};
          to->effect = builder_->EffectPhi(2, effects, merge);
        }
        // Merge SSA values.
        for (int i = decoder->NumLocals() - 1; i >= 0; i--) {
          TFNode* a = to->locals[i];
          TFNode* b = from->locals[i];
          if (a != b) {
            TFNode* vals[] = {a, b};
            to->locals[i] =
                builder_->Phi(decoder->GetLocalType(i), 2, vals, merge);
          }
        }
        break;
      }
      case SsaEnv::kMerged: {
        TFNode* merge = to->control;
        // Extend the existing merge.
        builder_->AppendToMerge(merge, from->control);
        // Merge effects.
        if (builder_->IsPhiWithMerge(to->effect, merge)) {
          builder_->AppendToPhi(to->effect, from->effect);
        } else if (to->effect != from->effect) {
          uint32_t count = builder_->InputCount(merge);
          TFNode** effects = builder_->Buffer(count);
          for (uint32_t j = 0; j < count - 1; j++) {
            effects[j] = to->effect;
          }
          effects[count - 1] = from->effect;
          to->effect = builder_->EffectPhi(count, effects, merge);
        }
        // Merge locals.
        for (int i = decoder->NumLocals() - 1; i >= 0; i--) {
          TFNode* tnode = to->locals[i];
          TFNode* fnode = from->locals[i];
          if (builder_->IsPhiWithMerge(tnode, merge)) {
            builder_->AppendToPhi(tnode, fnode);
          } else if (tnode != fnode) {
            uint32_t count = builder_->InputCount(merge);
            TFNode** vals = builder_->Buffer(count);
            for (uint32_t j = 0; j < count - 1; j++) {
              vals[j] = tnode;
            }
            vals[count - 1] = fnode;
            to->locals[i] =
                builder_->Phi(decoder->GetLocalType(i), count, vals, merge);
          }
        }
        break;
      }
      default:
        UNREACHABLE();
    }
    return from->Kill();
  }

  TFNode* CreateOrMergeIntoPhi(ValueType type, TFNode* merge, TFNode* tnode,
                               TFNode* fnode) {
    if (builder_->IsPhiWithMerge(tnode, merge)) {
      builder_->AppendToPhi(tnode, fnode);
    } else if (tnode != fnode) {
      uint32_t count = builder_->InputCount(merge);
      TFNode** vals = builder_->Buffer(count);
      for (uint32_t j = 0; j < count - 1; j++) vals[j] = tnode;
      vals[count - 1] = fnode;
      return builder_->Phi(type, count, vals, merge);
    }
    return tnode;
  }

  IMPL_RET(SsaEnv*, PrepareForLoop, SsaEnv* env) {
    if (!env->go()) return Split(decoder, env);
    env->state = SsaEnv::kMerged;

    env->control = builder_->Loop(env->control);
    env->effect = builder_->EffectPhi(1, &env->effect, env->control);
    builder_->Terminate(env->effect, env->control);
    BitVector* assigned = WasmDecoder<do_validation>::AnalyzeLoopAssignment(
        decoder, decoder->pc(), static_cast<int>(decoder->total_locals()),
        decoder->zone());
    if (decoder->failed()) return env;
    if (assigned != nullptr) {
      // Only introduce phis for variables assigned in this loop.
      for (int i = decoder->NumLocals() - 1; i >= 0; i--) {
        if (!assigned->Contains(i)) continue;
        env->locals[i] = builder_->Phi(decoder->GetLocalType(i), 1,
                                       &env->locals[i], env->control);
      }
      SsaEnv* loop_body_env = Split(decoder, env);
      builder_->StackCheck(decoder->position(), &(loop_body_env->effect),
                           &(loop_body_env->control));
      return loop_body_env;
    }

    // Conservatively introduce phis for all local variables.
    for (int i = decoder->NumLocals() - 1; i >= 0; i--) {
      env->locals[i] = builder_->Phi(decoder->GetLocalType(i), 1,
                                     &env->locals[i], env->control);
    }

    SsaEnv* loop_body_env = Split(decoder, env);
    builder_->StackCheck(decoder->position(), &loop_body_env->effect,
                         &loop_body_env->control);
    return loop_body_env;
  }

  // Create a complete copy of the {from}.
  IMPL_RET(SsaEnv*, Split, SsaEnv* from) {
    DCHECK_NOT_NULL(from);
    SsaEnv* result =
        reinterpret_cast<SsaEnv*>(decoder->zone()->New(sizeof(SsaEnv)));
    size_t size = sizeof(TFNode*) * decoder->NumLocals();
    result->control = from->control;
    result->effect = from->effect;

    if (from->go()) {
      result->state = SsaEnv::kReached;
      result->locals =
          size > 0 ? reinterpret_cast<TFNode**>(decoder->zone()->New(size))
                   : nullptr;
      memcpy(result->locals, from->locals, size);
    } else {
      result->state = SsaEnv::kUnreachable;
      result->locals = nullptr;
    }

    return result;
  }

  // Create a copy of {from} that steals its state and leaves {from}
  // unreachable.
  SsaEnv* Steal(Zone* zone, SsaEnv* from) {
    DCHECK_NOT_NULL(from);
    if (!from->go()) return UnreachableEnv(zone);
    SsaEnv* result = reinterpret_cast<SsaEnv*>(zone->New(sizeof(SsaEnv)));
    result->state = SsaEnv::kReached;
    result->locals = from->locals;
    result->control = from->control;
    result->effect = from->effect;
    from->Kill(SsaEnv::kUnreachable);
    return result;
  }

  // Create an unreachable environment.
  SsaEnv* UnreachableEnv(Zone* zone) {
    SsaEnv* result = reinterpret_cast<SsaEnv*>(zone->New(sizeof(SsaEnv)));
    result->state = SsaEnv::kUnreachable;
    result->control = nullptr;
    result->effect = nullptr;
    result->locals = nullptr;
    return result;
  }

  template <bool do_validation, typename Operand>
  void DoCall(
      WasmFullDecoder<do_validation, WasmGraphBuildingConsumer>* decoder,
      TFNode* index_node, const Operand& operand,
      const Value<WasmGraphBuildingConsumer> args[],
      Value<WasmGraphBuildingConsumer> returns[], bool is_indirect) {
    if (!build(decoder)) return;
    int param_count = static_cast<int>(operand.sig->parameter_count());
    TFNode** arg_nodes = builder_->Buffer(param_count + 1);
    TFNode** return_nodes = nullptr;
    arg_nodes[0] = index_node;
    for (int i = 0; i < param_count; ++i) {
      arg_nodes[i + 1] = args[i].consumer_data.node;
    }
    if (is_indirect) {
      builder_->CallIndirect(operand.index, arg_nodes, &return_nodes,
                             decoder->position());
    } else {
      builder_->CallDirect(operand.index, arg_nodes, &return_nodes,
                           decoder->position());
    }
    int return_count = static_cast<int>(operand.sig->return_count());
    for (int i = 0; i < return_count; ++i) {
      returns[i].consumer_data.node = return_nodes[i];
    }
  }
};

}  // namespace

bool DecodeLocalDecls(BodyLocalDecls* decls, const byte* start,
                      const byte* end) {
  Decoder decoder(start, end);
  if (WasmDecoder<true>::DecodeLocals(&decoder, nullptr, &decls->type_list)) {
    DCHECK(decoder.ok());
    decls->encoded_size = decoder.pc_offset();
    return true;
  }
  return false;
}

BytecodeIterator::BytecodeIterator(const byte* start, const byte* end,
                                   BodyLocalDecls* decls)
    : Decoder(start, end) {
  if (decls != nullptr) {
    if (DecodeLocalDecls(decls, start, end)) {
      pc_ += decls->encoded_size;
      if (pc_ > end_) pc_ = end_;
    }
  }
}

DecodeResult VerifyWasmCode(AccountingAllocator* allocator,
                            const wasm::WasmModule* module,
                            FunctionBody& body) {
  Zone zone(allocator, ZONE_NAME);
  WasmFullDecoder<true, EmptyConsumer> decoder(&zone, module, body);
  decoder.Decode();
  return decoder.toResult(nullptr);
}

DecodeResult VerifyWasmCodeWithStats(AccountingAllocator* allocator,
                                     const wasm::WasmModule* module,
                                     FunctionBody& body, bool is_wasm,
                                     Counters* counters) {
  auto size_histogram = is_wasm ? counters->wasm_wasm_function_size_bytes()
                                : counters->wasm_asm_function_size_bytes();
  // TODO(bradnelson): Improve histogram handling of ptrdiff_t.
  CHECK((body.end - body.start) >= 0);
  size_histogram->AddSample(static_cast<int>(body.end - body.start));
  auto time_counter = is_wasm ? counters->wasm_decode_wasm_function_time()
                              : counters->wasm_decode_asm_function_time();
  TimedHistogramScope wasm_decode_function_time_scope(time_counter);
  return VerifyWasmCode(allocator, module, body);
}

DecodeResult BuildTFGraph(AccountingAllocator* allocator, TFBuilder* builder,
                          FunctionBody& body) {
  Zone zone(allocator, ZONE_NAME);
  const wasm::WasmModule* module =
      builder->module_env() ? builder->module_env()->module : nullptr;
  WasmFullDecoder<true, WasmGraphBuildingConsumer> decoder(&zone, module, body,
                                                           builder);
  decoder.Decode();
  return decoder.toResult(nullptr);
}

unsigned OpcodeLength(const byte* pc, const byte* end) {
  Decoder decoder(pc, end);
  return WasmDecoder<false>::OpcodeLength(&decoder, pc);
}

std::pair<uint32_t, uint32_t> StackEffect(const WasmModule* module,
                                          FunctionSig* sig, const byte* pc,
                                          const byte* end) {
  WasmDecoder<false> decoder(module, sig, pc, end);
  return decoder.StackEffect(pc);
}

void PrintRawWasmCode(const byte* start, const byte* end) {
  AccountingAllocator allocator;
  PrintRawWasmCode(&allocator, FunctionBodyForTesting(start, end), nullptr);
}

namespace {
const char* RawOpcodeName(WasmOpcode opcode) {
  switch (opcode) {
#define DECLARE_NAME_CASE(name, opcode, sig) \
  case kExpr##name:                          \
    return "kExpr" #name;
    FOREACH_OPCODE(DECLARE_NAME_CASE)
#undef DECLARE_NAME_CASE
    default:
      break;
  }
  return "Unknown";
}
}  // namespace

bool PrintRawWasmCode(AccountingAllocator* allocator, const FunctionBody& body,
                      const wasm::WasmModule* module) {
  OFStream os(stdout);
  Zone zone(allocator, ZONE_NAME);
  WasmDecoder<false> decoder(module, body.sig, body.start, body.end);
  int line_nr = 0;

  // Print the function signature.
  if (body.sig) {
    os << "// signature: " << *body.sig << std::endl;
    ++line_nr;
  }

  // Print the local declarations.
  BodyLocalDecls decls(&zone);
  BytecodeIterator i(body.start, body.end, &decls);
  if (body.start != i.pc() && !FLAG_wasm_code_fuzzer_gen_test) {
    os << "// locals: ";
    if (!decls.type_list.empty()) {
      ValueType type = decls.type_list[0];
      uint32_t count = 0;
      for (size_t pos = 0; pos < decls.type_list.size(); ++pos) {
        if (decls.type_list[pos] == type) {
          ++count;
        } else {
          os << " " << count << " " << WasmOpcodes::TypeName(type);
          type = decls.type_list[pos];
          count = 1;
        }
      }
    }
    os << std::endl;
    ++line_nr;

    for (const byte* locals = body.start; locals < i.pc(); locals++) {
      os << (locals == body.start ? "0x" : " 0x") << AsHex(*locals, 2) << ",";
    }
    os << std::endl;
    ++line_nr;
  }

  os << "// body: " << std::endl;
  ++line_nr;
  unsigned control_depth = 0;
  for (; i.has_next(); i.next()) {
    unsigned length = WasmDecoder<false>::OpcodeLength(&decoder, i.pc());

    WasmOpcode opcode = i.current();
    if (opcode == kExprElse) control_depth--;

    int num_whitespaces = control_depth < 32 ? 2 * control_depth : 64;

    // 64 whitespaces
    const char* padding =
        "                                                                ";
    os.write(padding, num_whitespaces);

    os << RawOpcodeName(opcode) << ",";

    for (size_t j = 1; j < length; ++j) {
      os << " 0x" << AsHex(i.pc()[j], 2) << ",";
    }

    switch (opcode) {
      case kExprElse:
        os << "   // @" << i.pc_offset();
        control_depth++;
        break;
      case kExprLoop:
      case kExprIf:
      case kExprBlock:
      case kExprTry: {
        BlockTypeOperand<false> operand(&i, i.pc());
        os << "   // @" << i.pc_offset();
        for (unsigned i = 0; i < operand.arity; i++) {
          os << " " << WasmOpcodes::TypeName(operand.read_entry(i));
        }
        control_depth++;
        break;
      }
      case kExprEnd:
        os << "   // @" << i.pc_offset();
        control_depth--;
        break;
      case kExprBr: {
        BreakDepthOperand<false> operand(&i, i.pc());
        os << "   // depth=" << operand.depth;
        break;
      }
      case kExprBrIf: {
        BreakDepthOperand<false> operand(&i, i.pc());
        os << "   // depth=" << operand.depth;
        break;
      }
      case kExprBrTable: {
        BranchTableOperand<false> operand(&i, i.pc());
        os << " // entries=" << operand.table_count;
        break;
      }
      case kExprCallIndirect: {
        CallIndirectOperand<false> operand(&i, i.pc());
        os << "   // sig #" << operand.index;
        if (decoder.Complete(i.pc(), operand)) {
          os << ": " << *operand.sig;
        }
        break;
      }
      case kExprCallFunction: {
        CallFunctionOperand<false> operand(&i, i.pc());
        os << " // function #" << operand.index;
        if (decoder.Complete(i.pc(), operand)) {
          os << ": " << *operand.sig;
        }
        break;
      }
      default:
        break;
    }
    os << std::endl;
    ++line_nr;
  }

  return decoder.ok();
}

BitVector* AnalyzeLoopAssignmentForTesting(Zone* zone, size_t num_locals,
                                           const byte* start, const byte* end) {
  Decoder decoder(start, end);
  return WasmDecoder<true>::AnalyzeLoopAssignment(
      &decoder, start, static_cast<int>(num_locals), zone);
}

}  // namespace wasm
}  // namespace internal
}  // namespace v8
