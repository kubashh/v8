// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/signature.h"

#include "src/base/platform/elapsed-timer.h"
#include "src/bit-vector.h"
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

// Return the evaluation of `condition` if do_validation==true, DCHECK
// and always return true otherwise.
#define validate(condition)            \
  (do_validation ? (condition) : [&] { \
    DCHECK(condition);                 \
    return true;                       \
  }())
// Return the evaluation of `condition` if do_validation==true, DCHECK that it's
// false and always return false otherwise.
#define check_error(condition)         \
  (do_validation ? (condition) : [&] { \
    DCHECK(!(condition));              \
    return false;                      \
  }())

#if DEBUG
#define TRACE(...)                                    \
  do {                                                \
    if (FLAG_trace_wasm_decoder) PrintF(__VA_ARGS__); \
  } while (false)
#else
#define TRACE(...)
#endif

#define CHECK_PROTOTYPE_OPCODE(flag)                                           \
  if (this->module_ != nullptr && this->module_->is_asm_js()) {                \
    this->error("Opcode not supported for asmjs modules");                     \
  }                                                                            \
  if (!FLAG_experimental_wasm_##flag) {                                        \
    this->error("Invalid opcode (enable with --experimental-wasm-" #flag ")"); \
    break;                                                                     \
  }

#define PROTOTYPE_NOT_FUNCTIONAL(opcode)                        \
  this->errorf(this->pc_, "Prototype still not functional: %s", \
               WasmOpcodes::OpcodeName(opcode));

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

enum ControlKind {
  kControlIf,
  kControlIfElse,
  kControlBlock,
  kControlLoop,
  kControlTry,
  kControlTryCatch
};

// Macros that build nodes only if there is a graph and the current SSA
// environment is reachable from start. This avoids problems with malformed
// TF graphs when decoding inputs that have unreachable code.
#define BUILD(func, ...)                                                    \
  (build(decoder) ? CheckForException(decoder, builder_->func(__VA_ARGS__)) \
                  : nullptr)

template <typename T>
Vector<T> vec2vec(std::vector<T>& vec) {
  return Vector<T>(vec.data(), vec.size());
}

// Generic Wasm bytecode decoder with utilities for decoding operands,
// lengths, etc.
template <bool do_validation>
class WasmDecoder : public Decoder {
 public:
  WasmDecoder(const WasmModule* module, FunctionSig* sig, const byte* start,
              const byte* end, uint32_t buffer_offset = 0)
      : Decoder(start, end, buffer_offset),
        module_(module),
        sig_(sig),
        local_types_(nullptr) {}
  const WasmModule* module_;
  FunctionSig* sig_;

  ZoneVector<ValueType>* local_types_;

  size_t total_locals() const {
    return local_types_ == nullptr ? 0 : local_types_->size();
  }

  static bool DecodeLocals(Decoder* decoder, const FunctionSig* sig,
                           ZoneVector<ValueType>* type_list) {
    DCHECK_NOT_NULL(type_list);
    DCHECK_EQ(0, type_list->size());
    // Initialize from signature.
    if (sig != nullptr) {
      type_list->assign(sig->parameters().begin(), sig->parameters().end());
    }
    // Decode local declarations, if any.
    uint32_t entries = decoder->consume_u32v("local decls count");
    if (decoder->failed()) return false;

    TRACE("local decls count: %u\n", entries);
    while (entries-- > 0 && decoder->ok() && decoder->more()) {
      uint32_t count = decoder->consume_u32v("local count");
      if (decoder->failed()) return false;

      if ((count + type_list->size()) > kV8MaxWasmFunctionLocals) {
        decoder->error(decoder->pc() - 1, "local count too large");
        return false;
      }
      byte code = decoder->consume_u8("local type");
      if (decoder->failed()) return false;

      ValueType type;
      switch (code) {
        case kLocalI32:
          type = kWasmI32;
          break;
        case kLocalI64:
          type = kWasmI64;
          break;
        case kLocalF32:
          type = kWasmF32;
          break;
        case kLocalF64:
          type = kWasmF64;
          break;
        case kLocalS128:
          type = kWasmS128;
          break;
        default:
          decoder->error(decoder->pc() - 1, "invalid local type");
          return false;
      }
      type_list->insert(type_list->end(), count, type);
    }
    DCHECK(decoder->ok());
    return true;
  }

  static BitVector* AnalyzeLoopAssignment(Decoder* decoder, const byte* pc,
                                          int locals_count, Zone* zone) {
    if (pc >= decoder->end()) return nullptr;
    if (*pc != kExprLoop) return nullptr;

    BitVector* assigned = new (zone) BitVector(locals_count, zone);
    int depth = 0;
    // Iteratively process all AST nodes nested inside the loop.
    while (pc < decoder->end() && decoder->ok()) {
      WasmOpcode opcode = static_cast<WasmOpcode>(*pc);
      unsigned length = 1;
      switch (opcode) {
        case kExprLoop:
        case kExprIf:
        case kExprBlock:
        case kExprTry:
          length = OpcodeLength(decoder, pc);
          depth++;
          break;
        case kExprSetLocal:  // fallthru
        case kExprTeeLocal: {
          LocalIndexOperand<do_validation> operand(decoder, pc);
          if (assigned->length() > 0 &&
              operand.index < static_cast<uint32_t>(assigned->length())) {
            // Unverified code might have an out-of-bounds index.
            assigned->Add(operand.index);
          }
          length = 1 + operand.length;
          break;
        }
        case kExprEnd:
          depth--;
          break;
        default:
          length = OpcodeLength(decoder, pc);
          break;
      }
      if (depth <= 0) break;
      pc += length;
    }
    return decoder->ok() ? assigned : nullptr;
  }

  inline bool Validate(const byte* pc,
                       LocalIndexOperand<do_validation>& operand) {
    if (validate(operand.index < total_locals())) {
      if (local_types_) {
        operand.type = local_types_->at(operand.index);
      } else {
        operand.type = kWasmStmt;
      }
      return true;
    }
    errorf(pc + 1, "invalid local index: %u", operand.index);
    return false;
  }

  inline bool Validate(const byte* pc,
                       GlobalIndexOperand<do_validation>& operand) {
    if (validate(module_ != nullptr &&
                 operand.index < module_->globals.size())) {
      operand.global = &module_->globals[operand.index];
      operand.type = operand.global->type;
      return true;
    }
    errorf(pc + 1, "invalid global index: %u", operand.index);
    return false;
  }

  inline bool Complete(const byte* pc,
                       CallFunctionOperand<do_validation>& operand) {
    if (validate(module_ != nullptr &&
                 operand.index < module_->functions.size())) {
      operand.sig = module_->functions[operand.index].sig;
      return true;
    }
    return false;
  }

  inline bool Validate(const byte* pc,
                       CallFunctionOperand<do_validation>& operand) {
    if (Complete(pc, operand)) {
      return true;
    }
    errorf(pc + 1, "invalid function index: %u", operand.index);
    return false;
  }

  inline bool Complete(const byte* pc,
                       CallIndirectOperand<do_validation>& operand) {
    if (validate(module_ != nullptr &&
                 operand.index < module_->signatures.size())) {
      operand.sig = module_->signatures[operand.index];
      return true;
    }
    return false;
  }

  inline bool Validate(const byte* pc,
                       CallIndirectOperand<do_validation>& operand) {
    if (check_error(module_ == nullptr || module_->function_tables.empty())) {
      error("function table has to exist to execute call_indirect");
      return false;
    }
    if (Complete(pc, operand)) {
      return true;
    }
    errorf(pc + 1, "invalid signature index: #%u", operand.index);
    return false;
  }

  inline bool Validate(const byte* pc,
                       BreakDepthOperand<do_validation>& operand,
                       size_t control_depth) {
    if (validate(operand.depth < control_depth)) {
      return true;
    }
    errorf(pc + 1, "invalid break depth: %u", operand.depth);
    return false;
  }

  bool Validate(const byte* pc, BranchTableOperand<do_validation>& operand,
                size_t block_depth) {
    if (check_error(operand.table_count >= kV8MaxWasmFunctionSize)) {
      errorf(pc + 1, "invalid table count (> max function size): %u",
             operand.table_count);
      return false;
    }
    return checkAvailable(operand.table_count);
  }

  inline bool Validate(const byte* pc, WasmOpcode opcode,
                       SimdLaneOperand<do_validation>& operand) {
    uint8_t num_lanes = 0;
    switch (opcode) {
      case kExprF32x4ExtractLane:
      case kExprF32x4ReplaceLane:
      case kExprI32x4ExtractLane:
      case kExprI32x4ReplaceLane:
        num_lanes = 4;
        break;
      case kExprI16x8ExtractLane:
      case kExprI16x8ReplaceLane:
        num_lanes = 8;
        break;
      case kExprI8x16ExtractLane:
      case kExprI8x16ReplaceLane:
        num_lanes = 16;
        break;
      default:
        UNREACHABLE();
        break;
    }
    if (check_error(operand.lane < 0 || operand.lane >= num_lanes)) {
      error(pc_ + 2, "invalid lane index");
      return false;
    } else {
      return true;
    }
  }

  inline bool Validate(const byte* pc, WasmOpcode opcode,
                       SimdShiftOperand<do_validation>& operand) {
    uint8_t max_shift = 0;
    switch (opcode) {
      case kExprI32x4Shl:
      case kExprI32x4ShrS:
      case kExprI32x4ShrU:
        max_shift = 32;
        break;
      case kExprI16x8Shl:
      case kExprI16x8ShrS:
      case kExprI16x8ShrU:
        max_shift = 16;
        break;
      case kExprI8x16Shl:
      case kExprI8x16ShrS:
      case kExprI8x16ShrU:
        max_shift = 8;
        break;
      default:
        UNREACHABLE();
        break;
    }
    if (check_error(operand.shift < 0 || operand.shift >= max_shift)) {
      error(pc_ + 2, "invalid shift amount");
      return false;
    } else {
      return true;
    }
  }

  inline bool Validate(const byte* pc,
                       Simd8x16ShuffleOperand<do_validation>& operand) {
    uint8_t max_lane = 0;
    for (uint32_t i = 0; i < kSimd128Size; ++i)
      max_lane = std::max(max_lane, operand.shuffle[i]);
    // Shuffle indices must be in [0..31] for a 16 lane shuffle.
    if (check_error(max_lane > 2 * kSimd128Size)) {
      error(pc_ + 2, "invalid shuffle mask");
      return false;
    } else {
      return true;
    }
  }

  static unsigned OpcodeLength(Decoder* decoder, const byte* pc) {
    WasmOpcode opcode = static_cast<WasmOpcode>(*pc);
    switch (opcode) {
#define DECLARE_OPCODE_CASE(name, opcode, sig) case kExpr##name:
      FOREACH_LOAD_MEM_OPCODE(DECLARE_OPCODE_CASE)
      FOREACH_STORE_MEM_OPCODE(DECLARE_OPCODE_CASE)
#undef DECLARE_OPCODE_CASE
      {
        MemoryAccessOperand<do_validation> operand(decoder, pc, UINT32_MAX);
        return 1 + operand.length;
      }
      case kExprBr:
      case kExprBrIf: {
        BreakDepthOperand<do_validation> operand(decoder, pc);
        return 1 + operand.length;
      }
      case kExprSetGlobal:
      case kExprGetGlobal: {
        GlobalIndexOperand<do_validation> operand(decoder, pc);
        return 1 + operand.length;
      }

      case kExprCallFunction: {
        CallFunctionOperand<do_validation> operand(decoder, pc);
        return 1 + operand.length;
      }
      case kExprCallIndirect: {
        CallIndirectOperand<do_validation> operand(decoder, pc);
        return 1 + operand.length;
      }

      case kExprTry:
      case kExprIf:  // fall thru
      case kExprLoop:
      case kExprBlock: {
        BlockTypeOperand<do_validation> operand(decoder, pc);
        return 1 + operand.length;
      }

      case kExprSetLocal:
      case kExprTeeLocal:
      case kExprGetLocal:
      case kExprCatch: {
        LocalIndexOperand<do_validation> operand(decoder, pc);
        return 1 + operand.length;
      }
      case kExprBrTable: {
        BranchTableOperand<do_validation> operand(decoder, pc);
        BranchTableIterator<do_validation> iterator(decoder, operand);
        return 1 + iterator.length();
      }
      case kExprI32Const: {
        ImmI32Operand<do_validation> operand(decoder, pc);
        return 1 + operand.length;
      }
      case kExprI64Const: {
        ImmI64Operand<do_validation> operand(decoder, pc);
        return 1 + operand.length;
      }
      case kExprGrowMemory:
      case kExprMemorySize: {
        MemoryIndexOperand<do_validation> operand(decoder, pc);
        return 1 + operand.length;
      }
      case kExprF32Const:
        return 5;
      case kExprF64Const:
        return 9;
      case kSimdPrefix: {
        byte simd_index = decoder->read_u8<do_validation>(pc + 1, "simd_index");
        WasmOpcode opcode =
            static_cast<WasmOpcode>(kSimdPrefix << 8 | simd_index);
        switch (opcode) {
#define DECLARE_OPCODE_CASE(name, opcode, sig) case kExpr##name:
          FOREACH_SIMD_0_OPERAND_OPCODE(DECLARE_OPCODE_CASE)
#undef DECLARE_OPCODE_CASE
          return 2;
#define DECLARE_OPCODE_CASE(name, opcode, sig) case kExpr##name:
          FOREACH_SIMD_1_OPERAND_OPCODE(DECLARE_OPCODE_CASE)
#undef DECLARE_OPCODE_CASE
          return 3;
#define DECLARE_OPCODE_CASE(name, opcode, sig) case kExpr##name:
          FOREACH_SIMD_MEM_OPCODE(DECLARE_OPCODE_CASE)
#undef DECLARE_OPCODE_CASE
          {
            MemoryAccessOperand<do_validation> operand(decoder, pc + 1,
                                                       UINT32_MAX);
            return 2 + operand.length;
          }
          // Shuffles require a byte per lane, or 16 immediate bytes.
          case kExprS8x16Shuffle:
            return 2 + kSimd128Size;
          default:
            decoder->error(pc, "invalid SIMD opcode");
            return 2;
        }
      }
      default:
        return 1;
    }
  }

  std::pair<uint32_t, uint32_t> StackEffect(const byte* pc) {
    WasmOpcode opcode = static_cast<WasmOpcode>(*pc);
    // Handle "simple" opcodes with a fixed signature first.
    FunctionSig* sig = WasmOpcodes::Signature(opcode);
    if (!sig) sig = WasmOpcodes::AsmjsSignature(opcode);
    if (sig) return {sig->parameter_count(), sig->return_count()};
    if (WasmOpcodes::IsPrefixOpcode(opcode)) {
      opcode = static_cast<WasmOpcode>(opcode << 8 | *(pc + 1));
    }

#define DECLARE_OPCODE_CASE(name, opcode, sig) case kExpr##name:
    // clang-format off
    switch (opcode) {
      case kExprSelect:
        return {3, 1};
      case kExprS128StoreMem:
      FOREACH_STORE_MEM_OPCODE(DECLARE_OPCODE_CASE)
        return {2, 0};
      case kExprS128LoadMem:
      FOREACH_LOAD_MEM_OPCODE(DECLARE_OPCODE_CASE)
      case kExprTeeLocal:
      case kExprGrowMemory:
        return {1, 1};
      case kExprSetLocal:
      case kExprSetGlobal:
      case kExprDrop:
      case kExprBrIf:
      case kExprBrTable:
      case kExprIf:
        return {1, 0};
      case kExprGetLocal:
      case kExprGetGlobal:
      case kExprI32Const:
      case kExprI64Const:
      case kExprF32Const:
      case kExprF64Const:
      case kExprMemorySize:
        return {0, 1};
      case kExprCallFunction: {
        CallFunctionOperand<do_validation> operand(this, pc);
        CHECK(Complete(pc, operand));
        return {operand.sig->parameter_count(), operand.sig->return_count()};
      }
      case kExprCallIndirect: {
        CallIndirectOperand<do_validation> operand(this, pc);
        CHECK(Complete(pc, operand));
        // Indirect calls pop an additional argument for the table index.
        return {operand.sig->parameter_count() + 1,
                operand.sig->return_count()};
      }
      case kExprBr:
      case kExprBlock:
      case kExprLoop:
      case kExprEnd:
      case kExprElse:
      case kExprNop:
      case kExprReturn:
      case kExprUnreachable:
        return {0, 0};
      default:
        V8_Fatal(__FILE__, __LINE__, "unimplemented opcode: %x (%s)", opcode,
                 WasmOpcodes::OpcodeName(opcode));
        return {0, 0};
    }
#undef DECLARE_OPCODE_CASE
    // clang-format on
  }
};

// Name, args...
#define CONSUMER_FUNCTIONS(F)                                                \
  F(StartFunction)                                                           \
  F(StartFunctionBody, Control<Consumer>* block)                             \
  F(FinishFunction)                                                          \
  F(PopControl, const Control<Consumer>& block)                              \
  F(Throw, const Value<Consumer>&)                                           \
  F(Select, const Value<Consumer>& cond, const Value<Consumer>& fval,        \
    const Value<Consumer>& tval, Value<Consumer>* result)                    \
  F(UnOp, WasmOpcode opcode, FunctionSig*, const Value<Consumer>& value,     \
    Value<Consumer>* result)                                                 \
  F(BinOp, WasmOpcode opcode, FunctionSig*, const Value<Consumer>& lhs,      \
    const Value<Consumer>& rhs, Value<Consumer>* result)                     \
  F(I32Const, Value<Consumer>* result, int32_t value)                        \
  F(I64Const, Value<Consumer>* result, int64_t value)                        \
  F(F32Const, Value<Consumer>* result, float value)                          \
  F(F64Const, Value<Consumer>* result, double value)                         \
  F(DoReturn, Vector<Value<Consumer>> values)                                \
  F(GetLocal, Value<Consumer>* result,                                       \
    const LocalIndexOperand<do_validation>& operand)                         \
  F(SetLocal, const Value<Consumer>& value,                                  \
    const LocalIndexOperand<do_validation>& operand)                         \
  F(TeeLocal, const Value<Consumer>& value, Value<Consumer>* result,         \
    const LocalIndexOperand<do_validation>& operand)                         \
  F(GetGlobal, Value<Consumer>* result,                                      \
    const GlobalIndexOperand<do_validation>& operand)                        \
  F(SetGlobal, const Value<Consumer>& value,                                 \
    const GlobalIndexOperand<do_validation>& operand)                        \
  F(Unreachable)                                                             \
  F(FallThruTo, Control<Consumer>* c)                                        \
  F(If, const Value<Consumer>& cond, Control<Consumer>* if_block)            \
  F(Else, Control<Consumer>* if_block)                                       \
  F(BreakTo, Control<Consumer>* block)                                       \
  F(BrIf, const Value<Consumer>& cond, Control<Consumer>* block)             \
  F(EndControl, Control<Consumer>* block)                                    \
  F(Block, Control<Consumer>* block)                                         \
  F(Loop, Control<Consumer>* block)                                          \
  F(LoadMem, ValueType type, MachineType mem_type,                           \
    const MemoryAccessOperand<do_validation>& operand,                       \
    const Value<Consumer>& index, Value<Consumer>* result)                   \
  F(StoreMem, ValueType type, MachineType mem_type,                          \
    const MemoryAccessOperand<do_validation>& operand,                       \
    const Value<Consumer>& index, const Value<Consumer>& value)              \
  F(GrowMemory, const Value<Consumer>& value, Value<Consumer>* result)       \
  F(CurrentMemoryPages, Value<Consumer>* result)                             \
  F(CallDirect, const CallFunctionOperand<do_validation>& operand,           \
    const Value<Consumer> args[], Value<Consumer> returns[])                 \
  F(CallIndirect, const Value<Consumer>& index,                              \
    const CallIndirectOperand<do_validation>& operand,                       \
    const Value<Consumer> args[], Value<Consumer> returns[])                 \
  F(SimdLaneOp, WasmOpcode opcode,                                           \
    const SimdLaneOperand<do_validation>& operand,                           \
    const Vector<Value<Consumer>> inputs, Value<Consumer>* result)           \
  F(SimdShiftOp, WasmOpcode opcode,                                          \
    const SimdShiftOperand<do_validation>& operand,                          \
    const Value<Consumer>& input, Value<Consumer>* result)                   \
  F(Simd8x16ShuffleOp, const Simd8x16ShuffleOperand<do_validation>& operand, \
    const Value<Consumer>& input0, const Value<Consumer>& input1,            \
    Value<Consumer>* result)                                                 \
  F(SimdOp, WasmOpcode opcode, Vector<Value<Consumer>> args,                 \
    Value<Consumer>* result)                                                 \
  F(BrTable, const BranchTableOperand<do_validation>& operand,               \
    const Value<Consumer>& key)                                              \
  F(Catch, const LocalIndexOperand<do_validation>& operand,                  \
    Control<Consumer>* block)

#define DEFINE_CONSUMER_FUNCTOR(name, ...)        \
  struct visitor_functor_##name {                 \
    template <typename T, typename... Args>       \
    bool operator()(T& t, Args&&... args) {       \
      return t.name(std::forward<Args>(args)...); \
    }                                             \
  };
CONSUMER_FUNCTIONS(DEFINE_CONSUMER_FUNCTOR)
#undef DEFINE_CONSUMER_FUNCTOR

#define IMPL_RET(ret, name, ...)                   \
  template <bool do_validation, typename Consumer> \
  ret name(WasmFullDecoder<do_validation, Consumer>* decoder, ##__VA_ARGS__)

#define IMPL(name, ...) IMPL_RET(void, name, ##__VA_ARGS__)

static constexpr int32_t kNullCatch = -1;

// An entry on the value stack.
template <typename Consumer>
struct Value {
  const byte* pc;
  ValueType type;
  typename Consumer::CValue consumer_data;

  // Named constructors.
  static Value Unreachable(const byte* pc) {
    return {pc, kWasmVar, Consumer::CValue::Unreachable()};
  }

  static Value New(const byte* pc, ValueType type) {
    return {pc, type, Consumer::CValue::New()};
  }
};

template <typename Consumer>
struct MergeValues {
  uint32_t arity;
  union {
    Value<Consumer>* array;
    Value<Consumer> first;
  } vals;  // Either multiple values or a single value.

  Value<Consumer>& operator[](size_t i) {
    DCHECK_GT(arity, i);
    return arity == 1 ? vals.first : vals.array[i];
  }
};

// An entry on the control stack (i.e. if, block, loop, or try).
template <typename Consumer>
struct Control {
  const byte* pc;
  ControlKind kind;
  size_t stack_depth;  // stack height at the beginning of the construct.
  typename Consumer::CControl consumer_data;
  bool unreachable;  // The current block has been ended.

  // Values merged into the end of this control construct.
  MergeValues<Consumer> merge;

  inline bool is_if() const { return is_onearmed_if() || is_if_else(); }
  inline bool is_onearmed_if() const { return kind == kControlIf; }
  inline bool is_if_else() const { return kind == kControlIfElse; }
  inline bool is_block() const { return kind == kControlBlock; }
  inline bool is_loop() const { return kind == kControlLoop; }
  inline bool is_try() const { return is_incomplete_try() || is_try_catch(); }
  inline bool is_incomplete_try() const { return kind == kControlTry; }
  inline bool is_try_catch() const { return kind == kControlTryCatch; }

  // Named constructors.
  static Control Block(const byte* pc, size_t stack_depth) {
    return {pc, kControlBlock, stack_depth, Consumer::CControl::Block(), false,
            {}};
  }

  static Control If(const byte* pc, size_t stack_depth) {
    return {pc, kControlIf, stack_depth, Consumer::CControl::If(), false, {}};
  }

  static Control Loop(const byte* pc, size_t stack_depth) {
    return {pc, kControlLoop, stack_depth, Consumer::CControl::Loop(), false,
            {}};
  }

  static Control Try(const byte* pc, size_t stack_depth) {
    return {pc, kControlTry, stack_depth, Consumer::CControl::Try(), false, {}};
  }
};

template <bool do_validation, typename Consumer>
class WasmFullDecoder : public WasmDecoder<do_validation> {
  // All Value and Control types should be trivially copyable for
  // performance. We push and pop them, and store them in local variables.
  static_assert(IS_TRIVIALLY_COPYABLE(Value<Consumer>),
                "all Value<...> types should be trivially copyable");
  static_assert(IS_TRIVIALLY_COPYABLE(Control<Consumer>),
                "all Control<...> types should be trivially copyable");

 public:
  WasmFullDecoder(Zone* zone, const wasm::WasmModule* module,
                  const FunctionBody& body, Consumer consumer)
      : WasmDecoder<do_validation>(module, body.sig, body.start, body.end,
                                   body.offset),
        zone_(zone),
        consumer_(consumer),
        local_type_vec_(zone),
        stack_(zone),
        control_(zone),
        last_end_found_(false),
        current_catch_(kNullCatch) {
    this->local_types_ = &local_type_vec_;
  }

  bool Decode() {
    DCHECK(stack_.empty());
    DCHECK(control_.empty());

    if (FLAG_wasm_code_fuzzer_gen_test) {
      PrintRawWasmCode(this->start_, this->end_);
    }
    base::ElapsedTimer decode_timer;
    if (FLAG_trace_wasm_decode_time) {
      decode_timer.Start();
    }

    if (this->end_ < this->pc_) {
      this->error("function body end < start");
      return false;
    }

    DCHECK_EQ(0, this->local_types_->size());
    WasmDecoder<do_validation>::DecodeLocals(this, this->sig_,
                                             this->local_types_);
    consumer_.StartFunction(this);
    DecodeFunctionBody();
    consumer_.FinishFunction(this);

    if (this->failed()) return this->TraceFailed();

    if (!control_.empty()) {
      // Generate a better error message whether the unterminated control
      // structure is the function body block or an innner structure.
      if (control_.size() > 1) {
        this->error(control_.back().pc, "unterminated control structure");
      } else {
        this->error("function body must end with \"end\" opcode.");
      }
      return TraceFailed();
    }

    if (!last_end_found_) {
      this->error("function body must end with \"end\" opcode.");
      return false;
    }

    if (FLAG_trace_wasm_decode_time) {
      double ms = decode_timer.Elapsed().InMillisecondsF();
      PrintF("wasm-decode %s (%0.3f ms)\n\n", this->ok() ? "ok" : "failed", ms);
    } else {
      TRACE("wasm-decode %s\n\n", this->ok() ? "ok" : "failed");
    }

    return true;
  }

  bool TraceFailed() {
    TRACE("wasm-error module+%-6d func+%d: %s\n\n", this->error_offset_,
          this->GetBufferRelativeOffset(this->error_offset_),
          this->error_msg_.c_str());
    return false;
  }

  const char* SafeOpcodeNameAt(const byte* pc) {
    if (pc >= this->end_) return "<end>";
    return WasmOpcodes::OpcodeName(static_cast<WasmOpcode>(*pc));
  }

 private:
  static constexpr size_t kErrorMsgSize = 128;
  friend class WasmGraphBuildingConsumer;

  Zone* zone_;

  Consumer consumer_;

  ZoneVector<ValueType> local_type_vec_;  // types of local variables.
  ZoneVector<Value<Consumer>> stack_;     // stack of values.
  ZoneVector<Control<Consumer>> control_;  // stack of blocks, loops, and ifs.
  bool last_end_found_;

  int32_t current_catch_;

  bool CheckHasMemory() {
    if (validate(this->module_->has_memory)) return true;
    this->error(this->pc_ - 1, "memory instruction with no memory");
    return false;
  }

  // Decodes the body of a function.
  void DecodeFunctionBody() {
    TRACE("wasm-decode %p...%p (module+%u, %d bytes)\n",
          reinterpret_cast<const void*>(this->start()),
          reinterpret_cast<const void*>(this->end()), this->pc_offset(),
          static_cast<int>(this->end() - this->start()));

    // Set up initial function block.
    {
      auto* c = PushBlock();
      c->merge.arity = static_cast<uint32_t>(this->sig_->return_count());

      if (c->merge.arity == 1) {
        c->merge.vals.first =
            Value<Consumer>::New(this->pc_, this->sig_->GetReturn(0));
      } else if (c->merge.arity > 1) {
        c->merge.vals.array = zone_->NewArray<Value<Consumer>>(c->merge.arity);
        for (unsigned i = 0; i < c->merge.arity; i++) {
          c->merge.vals.array[i] =
              Value<Consumer>::New(this->pc_, this->sig_->GetReturn(i));
        }
      }
      consumer_.StartFunctionBody(this, c);
    }

    while (this->pc_ < this->end_) {  // decoding loop.
      unsigned len = 1;
      WasmOpcode opcode = static_cast<WasmOpcode>(*this->pc_);
#if DEBUG
      if (FLAG_trace_wasm_decoder && !WasmOpcodes::IsPrefixOpcode(opcode)) {
        TRACE("  @%-8d #%-20s|", startrel(this->pc_),
              WasmOpcodes::OpcodeName(opcode));
      }
#endif

      FunctionSig* sig = WasmOpcodes::Signature(opcode);
      if (sig) {
        BuildSimpleOperator(opcode, sig);
      } else {
        // Complex bytecode.
        switch (opcode) {
          case kExprNop:
            break;
          case kExprBlock: {
            BlockTypeOperand<do_validation> operand(this, this->pc_);
            auto* block = PushBlock();
            SetBlockType(block, operand);
            len = 1 + operand.length;
            consumer_.Block(this, block);
            break;
          }
          case kExprRethrow: {
            // TODO(kschimpf): Implement.
            CHECK_PROTOTYPE_OPCODE(eh);
            PROTOTYPE_NOT_FUNCTIONAL(opcode);
            break;
          }
          case kExprThrow: {
            // TODO(kschimpf): Fix to use type signature of exception.
            CHECK_PROTOTYPE_OPCODE(eh);
            PROTOTYPE_NOT_FUNCTIONAL(opcode);
            auto value = Pop(0, kWasmI32);
            consumer_.Throw(this, value);
            // TODO(titzer): Throw should end control, but currently we build a
            // (reachable) runtime call instead of connecting it directly to
            // end.
            //            EndControl();
            break;
          }
          case kExprTry: {
            CHECK_PROTOTYPE_OPCODE(eh);
            BlockTypeOperand<do_validation> operand(this, this->pc_);
            PushTry();
            SetBlockType(&control_.back(), operand);
            len = 1 + operand.length;
            break;
          }
          case kExprCatch: {
            // TODO(kschimpf): Fix to use type signature of exception.
            CHECK_PROTOTYPE_OPCODE(eh);
            PROTOTYPE_NOT_FUNCTIONAL(opcode);
            LocalIndexOperand<do_validation> operand(this, this->pc_);
            len = 1 + operand.length;

            if (check_error(control_.empty())) {
              this->error("catch does not match any try");
              break;
            }

            Control<Consumer>* c = &control_.back();
            if (check_error(c->is_try_catch())) {
              this->error(this->pc_,
                          "catch already present for try with catch");
              break;
            }

            if (check_error(!c->is_try())) {
              this->error("catch does not match any try");
              break;
            }
            c->kind = kControlTryCatch;

            FallThruTo(c);
            stack_.resize(c->stack_depth);

            if (!this->Validate(this->pc_, operand)) break;
            consumer_.Catch(this, operand, c);

            break;
          }
          case kExprCatchAll: {
            // TODO(kschimpf): Implement.
            CHECK_PROTOTYPE_OPCODE(eh);
            PROTOTYPE_NOT_FUNCTIONAL(opcode);
            break;
          }
          case kExprLoop: {
            BlockTypeOperand<do_validation> operand(this, this->pc_);
            // The continue environment is the inner environment.
            auto* block = PushLoop();
            SetBlockType(&control_.back(), operand);
            len = 1 + operand.length;
            consumer_.Loop(this, block);
            break;
          }
          case kExprIf: {
            // Condition on top of stack. Split environments for branches.
            BlockTypeOperand<do_validation> operand(this, this->pc_);
            auto cond = Pop(0, kWasmI32);
            auto* if_block = PushIf();
            SetBlockType(if_block, operand);
            consumer_.If(this, cond, if_block);
            len = 1 + operand.length;
            break;
          }
          case kExprElse: {
            if (check_error(control_.empty())) {
              this->error("else does not match any if");
              break;
            }
            Control<Consumer>* c = &control_.back();
            if (check_error(!c->is_if())) {
              this->error(this->pc_, "else does not match an if");
              break;
            }
            if (c->is_if_else()) {
              this->error(this->pc_, "else already present for if");
              break;
            }
            c->kind = kControlIfElse;
            FallThruTo(c);
            stack_.resize(c->stack_depth);
            consumer_.Else(this, c);
            break;
          }
          case kExprEnd: {
            if (check_error(control_.empty())) {
              this->error("end does not match any if, try, or block");
              return;
            }
            Control<Consumer>* c = &control_.back();
            if (c->is_loop()) {
              // A loop just leaves the values on the stack.
              TypeCheckFallThru(c);
              if (c->unreachable) PushEndValues(c);
              PopControl(c);
              break;
            }
            if (c->is_onearmed_if()) {
              // End the true branch of a one-armed if.
              if (check_error(!c->unreachable &&
                              stack_.size() != c->stack_depth)) {
                this->error("end of if expected empty stack");
                stack_.resize(c->stack_depth);
              }
              if (check_error(c->merge.arity > 0)) {
                this->error("non-void one-armed if");
              }
            } else if (check_error(c->is_incomplete_try())) {
              this->error(this->pc_, "missing catch in try");
              break;
            }
            FallThruTo(c);
            PushEndValues(c);

            if (control_.size() == 1) {
              // If at the last (implicit) control, check we are at end.
              if (check_error(this->pc_ + 1 != this->end_)) {
                this->error(this->pc_ + 1, "trailing code after function end");
                break;
              }
              last_end_found_ = true;
              if (c->unreachable) {
                TypeCheckFallThru(c);
              } else {
                // The result of the block is the return value.
                TRACE("  @%-8d #xx:%-20s|", startrel(this->pc_),
                      "(implicit) return");
                DoReturn();
                TRACE("\n");
              }
            }
            PopControl(c);
            break;
          }
          case kExprSelect: {
            auto cond = Pop(2, kWasmI32);
            auto fval = Pop();
            auto tval = Pop(0, fval.type);
            auto* result = Push(tval.type == kWasmVar ? fval.type : tval.type);
            consumer_.Select(this, cond, fval, tval, result);
            break;
          }
          case kExprBr: {
            BreakDepthOperand<do_validation> operand(this, this->pc_);
            if (validate(this->Validate(this->pc_, operand, control_.size()) &&
                         TypeCheckBreak(operand.depth))) {
              consumer_.BreakTo(this, GetControl(operand.depth));
            }
            len = 1 + operand.length;
            EndControl();
            break;
          }
          case kExprBrIf: {
            BreakDepthOperand<do_validation> operand(this, this->pc_);
            auto cond = Pop(0, kWasmI32);
            if (validate(this->ok() &&
                         this->Validate(this->pc_, operand, control_.size()) &&
                         TypeCheckBreak(operand.depth))) {
              consumer_.BrIf(this, cond, GetControl(operand.depth));
            }
            len = 1 + operand.length;
            break;
          }
          case kExprBrTable: {
            BranchTableOperand<do_validation> operand(this, this->pc_);
            BranchTableIterator<do_validation> iterator(this, operand);
            if (!this->Validate(this->pc_, operand, control_.size())) break;
            auto key = Pop(0, kWasmI32);
            MergeValues<Consumer>* merge = nullptr;
            while (iterator.has_next()) {
              const uint32_t i = iterator.cur_index();
              const byte* pos = iterator.pc();
              uint32_t target = iterator.next();
              if (check_error(target >= control_.size())) {
                this->error(pos, "improper branch in br_table");
                break;
              }
              // Check that label types match up.
              static MergeValues<Consumer> loop_dummy = {0, {nullptr}};
              Control<Consumer>* c = GetControl(target);
              MergeValues<Consumer>* current =
                  c->is_loop() ? &loop_dummy : &c->merge;
              if (i == 0) {
                merge = current;
              } else if (check_error(merge->arity != current->arity)) {
                this->errorf(pos,
                             "inconsistent arity in br_table target %d"
                             " (previous was %u, this one %u)",
                             i, merge->arity, current->arity);
              } else if (GetControl(0)->unreachable) {
                for (uint32_t j = 0; validate(this->ok()) && j < merge->arity;
                     ++j) {
                  if (check_error((*merge)[j].type != (*current)[j].type)) {
                    this->errorf(pos,
                                 "type error in br_table target %d operand %d"
                                 " (previous expected %s, this one %s)",
                                 i, j, WasmOpcodes::TypeName((*merge)[j].type),
                                 WasmOpcodes::TypeName((*current)[j].type));
                  }
                }
              }
              bool valid = TypeCheckBreak(target);
              if (check_error(!valid)) break;
            }
            if (check_error(this->failed())) break;

            if (operand.table_count > 0) {
              consumer_.BrTable(this, operand, key);
            } else {
              // Only a default target. Do the equivalent of br.
              BranchTableIterator<do_validation> iterator(this, operand);
              const byte* pos = iterator.pc();
              uint32_t target = iterator.next();
              if (check_error(target >= control_.size())) {
                this->error(pos, "improper branch in br_table");
                break;
              }
              consumer_.BreakTo(this, GetControl(target));
            }
            len = 1 + iterator.length();
            EndControl();
            break;
          }
          case kExprReturn: {
            DoReturn();
            break;
          }
          case kExprUnreachable: {
            consumer_.Unreachable(this);
            EndControl();
            break;
          }
          case kExprI32Const: {
            ImmI32Operand<do_validation> operand(this, this->pc_);
            auto* value = Push(kWasmI32);
            consumer_.I32Const(this, value, operand.value);
            len = 1 + operand.length;
            break;
          }
          case kExprI64Const: {
            ImmI64Operand<do_validation> operand(this, this->pc_);
            auto* value = Push(kWasmI64);
            consumer_.I64Const(this, value, operand.value);
            len = 1 + operand.length;
            break;
          }
          case kExprF32Const: {
            ImmF32Operand<do_validation> operand(this, this->pc_);
            auto* value = Push(kWasmF32);
            consumer_.F32Const(this, value, operand.value);
            len = 1 + operand.length;
            break;
          }
          case kExprF64Const: {
            ImmF64Operand<do_validation> operand(this, this->pc_);
            auto* value = Push(kWasmF64);
            consumer_.F64Const(this, value, operand.value);
            len = 1 + operand.length;
            break;
          }
          case kExprGetLocal: {
            LocalIndexOperand<do_validation> operand(this, this->pc_);
            if (!this->Validate(this->pc_, operand)) break;
            auto* value = Push(operand.type);
            consumer_.GetLocal(this, value, operand);
            len = 1 + operand.length;
            break;
          }
          case kExprSetLocal: {
            LocalIndexOperand<do_validation> operand(this, this->pc_);
            if (!this->Validate(this->pc_, operand)) break;
            auto value = Pop(0, local_type_vec_[operand.index]);
            consumer_.SetLocal(this, value, operand);
            len = 1 + operand.length;
            break;
          }
          case kExprTeeLocal: {
            LocalIndexOperand<do_validation> operand(this, this->pc_);
            if (!this->Validate(this->pc_, operand)) break;
            auto value = Pop(0, local_type_vec_[operand.index]);
            auto* result = Push(value.type);
            consumer_.TeeLocal(this, value, result, operand);
            len = 1 + operand.length;
            break;
          }
          case kExprDrop: {
            Pop();
            break;
          }
          case kExprGetGlobal: {
            GlobalIndexOperand<do_validation> operand(this, this->pc_);
            len = 1 + operand.length;
            if (!this->Validate(this->pc_, operand)) break;
            auto* result = Push(operand.type);
            consumer_.GetGlobal(this, result, operand);
            break;
          }
          case kExprSetGlobal: {
            GlobalIndexOperand<do_validation> operand(this, this->pc_);
            len = 1 + operand.length;
            if (!this->Validate(this->pc_, operand)) break;
            if (check_error(!operand.global->mutability)) {
              this->errorf(this->pc_, "immutable global #%u cannot be assigned",
                           operand.index);
              break;
            }
            auto value = Pop(0, operand.type);
            consumer_.SetGlobal(this, value, operand);
            break;
          }
          case kExprI32LoadMem8S:
            len = DecodeLoadMem(kWasmI32, MachineType::Int8());
            break;
          case kExprI32LoadMem8U:
            len = DecodeLoadMem(kWasmI32, MachineType::Uint8());
            break;
          case kExprI32LoadMem16S:
            len = DecodeLoadMem(kWasmI32, MachineType::Int16());
            break;
          case kExprI32LoadMem16U:
            len = DecodeLoadMem(kWasmI32, MachineType::Uint16());
            break;
          case kExprI32LoadMem:
            len = DecodeLoadMem(kWasmI32, MachineType::Int32());
            break;
          case kExprI64LoadMem8S:
            len = DecodeLoadMem(kWasmI64, MachineType::Int8());
            break;
          case kExprI64LoadMem8U:
            len = DecodeLoadMem(kWasmI64, MachineType::Uint8());
            break;
          case kExprI64LoadMem16S:
            len = DecodeLoadMem(kWasmI64, MachineType::Int16());
            break;
          case kExprI64LoadMem16U:
            len = DecodeLoadMem(kWasmI64, MachineType::Uint16());
            break;
          case kExprI64LoadMem32S:
            len = DecodeLoadMem(kWasmI64, MachineType::Int32());
            break;
          case kExprI64LoadMem32U:
            len = DecodeLoadMem(kWasmI64, MachineType::Uint32());
            break;
          case kExprI64LoadMem:
            len = DecodeLoadMem(kWasmI64, MachineType::Int64());
            break;
          case kExprF32LoadMem:
            len = DecodeLoadMem(kWasmF32, MachineType::Float32());
            break;
          case kExprF64LoadMem:
            len = DecodeLoadMem(kWasmF64, MachineType::Float64());
            break;
          case kExprI32StoreMem8:
            len = DecodeStoreMem(kWasmI32, MachineType::Int8());
            break;
          case kExprI32StoreMem16:
            len = DecodeStoreMem(kWasmI32, MachineType::Int16());
            break;
          case kExprI32StoreMem:
            len = DecodeStoreMem(kWasmI32, MachineType::Int32());
            break;
          case kExprI64StoreMem8:
            len = DecodeStoreMem(kWasmI64, MachineType::Int8());
            break;
          case kExprI64StoreMem16:
            len = DecodeStoreMem(kWasmI64, MachineType::Int16());
            break;
          case kExprI64StoreMem32:
            len = DecodeStoreMem(kWasmI64, MachineType::Int32());
            break;
          case kExprI64StoreMem:
            len = DecodeStoreMem(kWasmI64, MachineType::Int64());
            break;
          case kExprF32StoreMem:
            len = DecodeStoreMem(kWasmF32, MachineType::Float32());
            break;
          case kExprF64StoreMem:
            len = DecodeStoreMem(kWasmF64, MachineType::Float64());
            break;
          case kExprGrowMemory: {
            if (!CheckHasMemory()) break;
            MemoryIndexOperand<do_validation> operand(this, this->pc_);
            len = 1 + operand.length;
            DCHECK_NOT_NULL(this->module_);
            if (check_error(!this->module_->is_wasm())) {
              this->error("grow_memory is not supported for asmjs modules");
              break;
            }
            auto value = Pop(0, kWasmI32);
            auto* result = Push(kWasmI32);
            consumer_.GrowMemory(this, value, result);
            break;
          }
          case kExprMemorySize: {
            if (!CheckHasMemory()) break;
            MemoryIndexOperand<do_validation> operand(this, this->pc_);
            auto* result = Push(kWasmI32);
            len = 1 + operand.length;
            consumer_.CurrentMemoryPages(this, result);
            break;
          }
          case kExprCallFunction: {
            CallFunctionOperand<do_validation> operand(this, this->pc_);
            len = 1 + operand.length;
            if (!this->Validate(this->pc_, operand)) break;
            // TODO(clemensh): Better memory management.
            std::vector<Value<Consumer>> args;
            PopArgs(operand.sig, &args);
            auto* returns = PushReturns(operand.sig);
            consumer_.CallDirect(this, operand, args.data(), returns);
            break;
          }
          case kExprCallIndirect: {
            CallIndirectOperand<do_validation> operand(this, this->pc_);
            len = 1 + operand.length;
            if (!this->Validate(this->pc_, operand)) break;
            auto index = Pop(0, kWasmI32);
            // TODO(clemensh): Better memory management.
            std::vector<Value<Consumer>> args;
            PopArgs(operand.sig, &args);
            auto* returns = PushReturns(operand.sig);
            consumer_.CallIndirect(this, index, operand, args.data(), returns);
            break;
          }
          case kSimdPrefix: {
            CHECK_PROTOTYPE_OPCODE(simd);
            len++;
            byte simd_index = this->template read_u8<do_validation>(
                this->pc_ + 1, "simd index");
            opcode = static_cast<WasmOpcode>(opcode << 8 | simd_index);
            TRACE("  @%-4d #%-20s|", startrel(this->pc_),
                  WasmOpcodes::OpcodeName(opcode));
            len += DecodeSimdOpcode(opcode);
            break;
          }
          case kAtomicPrefix: {
            if (this->module_ == nullptr || !this->module_->is_asm_js()) {
              this->error("Atomics are allowed only in AsmJs modules");
              break;
            }
            CHECK_PROTOTYPE_OPCODE(threads);
            len = 2;
            byte atomic_opcode = this->template read_u8<do_validation>(
                this->pc_ + 1, "atomic index");
            opcode = static_cast<WasmOpcode>(opcode << 8 | atomic_opcode);
            sig = WasmOpcodes::AtomicSignature(opcode);
            if (sig) {
              BuildAtomicOperator(opcode);
            }
            break;
          }
          default: {
            // Deal with special asmjs opcodes.
            if (this->module_ != nullptr && this->module_->is_asm_js()) {
              sig = WasmOpcodes::AsmjsSignature(opcode);
              if (sig) {
                BuildSimpleOperator(opcode, sig);
              }
            } else {
              this->error("Invalid opcode");
              return;
            }
          }
        }
      }

#if DEBUG
      if (FLAG_trace_wasm_decoder) {
        PrintF(" ");
        for (size_t i = 0; i < control_.size(); ++i) {
          Control<Consumer>* c = &control_[i];
          switch (c->kind) {
            case kControlIf:
              PrintF("I");
              break;
            case kControlBlock:
              PrintF("B");
              break;
            case kControlLoop:
              PrintF("L");
              break;
            case kControlTry:
              PrintF("T");
              break;
            default:
              break;
          }
          PrintF("%u", c->merge.arity);
          if (c->unreachable) PrintF("*");
        }
        PrintF(" | ");
        for (size_t i = 0; i < stack_.size(); ++i) {
          auto& val = stack_[i];
          WasmOpcode opcode = static_cast<WasmOpcode>(*val.pc);
          if (WasmOpcodes::IsPrefixOpcode(opcode)) {
            opcode = static_cast<WasmOpcode>(opcode << 8 | *(val.pc + 1));
          }
          PrintF(" %c@%d:%s", WasmOpcodes::ShortNameOf(val.type),
                 static_cast<int>(val.pc - this->start_),
                 WasmOpcodes::OpcodeName(opcode));
          switch (opcode) {
            case kExprI32Const: {
              ImmI32Operand<do_validation> operand(this, val.pc);
              PrintF("[%d]", operand.value);
              break;
            }
            case kExprGetLocal: {
              LocalIndexOperand<do_validation> operand(this, val.pc);
              PrintF("[%u]", operand.index);
              break;
            }
            case kExprSetLocal:  // fallthru
            case kExprTeeLocal: {
              LocalIndexOperand<do_validation> operand(this, val.pc);
              PrintF("[%u]", operand.index);
              break;
            }
            default:
              break;
          }
        }
        PrintF("\n");
      }
#endif
      this->pc_ += len;
    }  // end decode loop
    if (this->pc_ > this->end_ && this->ok()) this->error("Beyond end of code");
  }

  void EndControl() {
    DCHECK(!control_.empty());
    auto* current = &control_.back();
    stack_.resize(current->stack_depth);
    current->unreachable = true;
    consumer_.EndControl(this, current);
  }

  void SetBlockType(Control<Consumer>* c,
                    BlockTypeOperand<do_validation>& operand) {
    c->merge.arity = operand.arity;
    if (c->merge.arity == 1) {
      c->merge.vals.first =
          Value<Consumer>::New(this->pc_, operand.read_entry(0));
    } else if (c->merge.arity > 1) {
      c->merge.vals.array = zone_->NewArray<Value<Consumer>>(c->merge.arity);
      for (unsigned i = 0; i < c->merge.arity; i++) {
        c->merge.vals.array[i] =
            Value<Consumer>::New(this->pc_, operand.read_entry(i));
      }
    }
  }

  // TODO(clemensh): Better memory management.
  void PopArgs(FunctionSig* sig, std::vector<Value<Consumer>>* result) {
    DCHECK(result->empty());
    int count = static_cast<int>(sig->parameter_count());
    result->resize(count);
    for (int i = count - 1; i >= 0; --i) {
      (*result)[i] = Pop(i, sig->GetParam(i));
    }
  }

  ValueType GetReturnType(FunctionSig* sig) {
    DCHECK_GE(1, sig->return_count());
    return sig->return_count() == 0 ? kWasmStmt : sig->GetReturn();
  }

  Control<Consumer>* PushBlock() {
    control_.emplace_back(Control<Consumer>::Block(this->pc_, stack_.size()));
    return &control_.back();
  }

  Control<Consumer>* PushLoop() {
    control_.emplace_back(Control<Consumer>::Loop(this->pc_, stack_.size()));
    return &control_.back();
  }

  Control<Consumer>* PushIf() {
    control_.emplace_back(Control<Consumer>::If(this->pc_, stack_.size()));
    return &control_.back();
  }

  Control<Consumer>* PushTry() {
    control_.emplace_back(Control<Consumer>::Try(this->pc_, stack_.size()));
    // current_catch_ = static_cast<int32_t>(control_.size() - 1);
    return &control_.back();
  }

  void PopControl(Control<Consumer>* c) {
    DCHECK_EQ(c, &control_.back());
    consumer_.PopControl(this, *c);
    control_.pop_back();
  }

  int DecodeLoadMem(ValueType type, MachineType mem_type) {
    if (!CheckHasMemory()) return 0;
    MemoryAccessOperand<do_validation> operand(
        this, this->pc_, ElementSizeLog2Of(mem_type.representation()));

    auto index = Pop(0, kWasmI32);
    auto* result = Push(type);
    consumer_.LoadMem(this, type, mem_type, operand, index, result);
    return 1 + operand.length;
  }

  int DecodeStoreMem(ValueType type, MachineType mem_type) {
    if (!CheckHasMemory()) return 0;
    MemoryAccessOperand<do_validation> operand(
        this, this->pc_, ElementSizeLog2Of(mem_type.representation()));
    auto value = Pop(1, type);
    auto index = Pop(0, kWasmI32);
    consumer_.StoreMem(this, type, mem_type, operand, index, value);
    return 1 + operand.length;
  }

  int DecodePrefixedLoadMem(ValueType type, MachineType mem_type) {
    if (!CheckHasMemory()) return 0;
    MemoryAccessOperand<do_validation> operand(
        this, this->pc_ + 1, ElementSizeLog2Of(mem_type.representation()));

    auto index = Pop(0, kWasmI32);
    auto* result = Push(type);
    consumer_.LoadMem(this, type, mem_type, operand, index, result);
    return operand.length;
  }

  int DecodePrefixedStoreMem(ValueType type, MachineType mem_type) {
    if (!CheckHasMemory()) return 0;
    MemoryAccessOperand<do_validation> operand(
        this, this->pc_ + 1, ElementSizeLog2Of(mem_type.representation()));
    auto value = Pop(1, type);
    auto index = Pop(0, kWasmI32);
    consumer_.StoreMem(this, type, mem_type, operand, index, value);
    return operand.length;
  }

  unsigned SimdExtractLane(WasmOpcode opcode, ValueType type) {
    SimdLaneOperand<do_validation> operand(this, this->pc_);
    if (this->Validate(this->pc_, opcode, operand)) {
      Value<Consumer> inputs[] = {Pop(0, ValueType::kSimd128)};
      auto* result = Push(type);
      consumer_.SimdLaneOp(this, opcode, operand, ArrayVector(inputs), result);
    }
    return operand.length;
  }

  unsigned SimdReplaceLane(WasmOpcode opcode, ValueType type) {
    SimdLaneOperand<do_validation> operand(this, this->pc_);
    if (this->Validate(this->pc_, opcode, operand)) {
      Value<Consumer> inputs[2];
      inputs[1] = Pop(1, type);
      inputs[0] = Pop(0, ValueType::kSimd128);
      auto* result = Push(ValueType::kSimd128);
      consumer_.SimdLaneOp(this, opcode, operand, ArrayVector(inputs), result);
    }
    return operand.length;
  }

  unsigned SimdShiftOp(WasmOpcode opcode) {
    SimdShiftOperand<do_validation> operand(this, this->pc_);
    if (this->Validate(this->pc_, opcode, operand)) {
      auto input = Pop(0, ValueType::kSimd128);
      auto* result = Push(ValueType::kSimd128);
      consumer_.SimdShiftOp(this, opcode, operand, input, result);
    }
    return operand.length;
  }

  unsigned Simd8x16ShuffleOp() {
    Simd8x16ShuffleOperand<do_validation> operand(this, this->pc_);
    if (this->Validate(this->pc_, operand)) {
      auto input1 = Pop(1, ValueType::kSimd128);
      auto input0 = Pop(0, ValueType::kSimd128);
      auto* result = Push(ValueType::kSimd128);
      consumer_.Simd8x16ShuffleOp(this, operand, input0, input1, result);
    }
    return 16;
  }

  unsigned DecodeSimdOpcode(WasmOpcode opcode) {
    unsigned len = 0;
    switch (opcode) {
      case kExprF32x4ExtractLane: {
        len = SimdExtractLane(opcode, ValueType::kFloat32);
        break;
      }
      case kExprI32x4ExtractLane:
      case kExprI16x8ExtractLane:
      case kExprI8x16ExtractLane: {
        len = SimdExtractLane(opcode, ValueType::kWord32);
        break;
      }
      case kExprF32x4ReplaceLane: {
        len = SimdReplaceLane(opcode, ValueType::kFloat32);
        break;
      }
      case kExprI32x4ReplaceLane:
      case kExprI16x8ReplaceLane:
      case kExprI8x16ReplaceLane: {
        len = SimdReplaceLane(opcode, ValueType::kWord32);
        break;
      }
      case kExprI32x4Shl:
      case kExprI32x4ShrS:
      case kExprI32x4ShrU:
      case kExprI16x8Shl:
      case kExprI16x8ShrS:
      case kExprI16x8ShrU:
      case kExprI8x16Shl:
      case kExprI8x16ShrS:
      case kExprI8x16ShrU: {
        len = SimdShiftOp(opcode);
        break;
      }
      case kExprS8x16Shuffle: {
        len = Simd8x16ShuffleOp();
        break;
      }
      case kExprS128LoadMem:
        len = DecodePrefixedLoadMem(kWasmS128, MachineType::Simd128());
        break;
      case kExprS128StoreMem:
        len = DecodePrefixedStoreMem(kWasmS128, MachineType::Simd128());
        break;
      default: {
        FunctionSig* sig = WasmOpcodes::Signature(opcode);
        if (check_error(sig == nullptr)) {
          this->error("invalid simd opcode");
          break;
        }
        std::vector<Value<Consumer>> args;
        PopArgs(sig, &args);
        auto* result =
            sig->return_count() == 0 ? nullptr : Push(GetReturnType(sig));
        consumer_.SimdOp(this, opcode, vec2vec(args), result);
      }
    }
    return len;
  }

  void BuildAtomicOperator(WasmOpcode opcode) { UNIMPLEMENTED(); }

  void DoReturn() {
    // TODO(clemensh): Optimize memory usage here (it will be mostly 0 or 1
    // returned values).
    int return_count = static_cast<int>(this->sig_->return_count());
    std::vector<Value<Consumer>> values(return_count);

    // Pop return values off the stack in reverse order.
    for (int i = return_count - 1; i >= 0; --i) {
      values[i] = Pop(i, this->sig_->GetReturn(i));
    }

    consumer_.DoReturn(this, vec2vec(values));
    EndControl();
  }

  inline Value<Consumer>* Push(ValueType type) {
    DCHECK(type != kWasmStmt);
    stack_.push_back(Value<Consumer>::New(this->pc_, type));
    return &stack_.back();
  }

  void PushEndValues(Control<Consumer>* c) {
    DCHECK_EQ(c, &control_.back());
    stack_.resize(c->stack_depth);
    if (c->merge.arity == 1) {
      stack_.push_back(c->merge.vals.first);
    } else {
      for (unsigned i = 0; i < c->merge.arity; i++) {
        stack_.push_back(c->merge.vals.array[i]);
      }
    }
    DCHECK_EQ(c->stack_depth + c->merge.arity, stack_.size());
  }

  Value<Consumer>* PushReturns(FunctionSig* sig) {
    size_t return_count = sig->return_count();
    if (return_count == 0) return nullptr;
    size_t old_size = stack_.size();
    for (size_t i = 0; i < return_count; ++i) {
      Push(sig->GetReturn(i));
    }
    return stack_.data() + old_size;
  }

  Value<Consumer> Pop(int index, ValueType expected) {
    auto val = Pop();
    if (check_error(val.type != expected && val.type != kWasmVar &&
                    expected != kWasmVar)) {
      this->errorf(val.pc, "%s[%d] expected type %s, found %s of type %s",
                   SafeOpcodeNameAt(this->pc_), index,
                   WasmOpcodes::TypeName(expected), SafeOpcodeNameAt(val.pc),
                   WasmOpcodes::TypeName(val.type));
    }
    return val;
  }

  Value<Consumer> Pop() {
    DCHECK(!control_.empty());
    size_t limit = control_.back().stack_depth;
    if (stack_.size() <= limit) {
      // Popping past the current control start in reachable code.
      if (check_error(!control_.back().unreachable)) {
        this->errorf(this->pc_, "%s found empty stack",
                     SafeOpcodeNameAt(this->pc_));
      }
      return Value<Consumer>::Unreachable(this->pc_);
    }
    auto val = stack_.back();
    stack_.pop_back();
    return val;
  }

  int startrel(const byte* ptr) { return static_cast<int>(ptr - this->start_); }

  inline Control<Consumer>* GetControl(unsigned depth) {
    DCHECK_GT(control_.size(), depth);
    return &control_[control_.size() - depth - 1];
  }

  bool TypeCheckBreak(unsigned depth) {
    DCHECK(do_validation);  // Only call this for validation.
    Control<Consumer>* c = GetControl(depth);
    if (c->is_loop()) {
      // This is the inner loop block, which does not have a value.
      return true;
    }
    size_t expected = control_.back().stack_depth + c->merge.arity;
    if (stack_.size() < expected && !control_.back().unreachable) {
      this->errorf(
          this->pc_,
          "expected at least %u values on the stack for br to @%d, found %d",
          c->merge.arity, startrel(c->pc),
          static_cast<int>(stack_.size() - c->stack_depth));
      return false;
    }

    return TypeCheckMergeValues(c);
  }

  void FallThruTo(Control<Consumer>* c) {
    DCHECK_EQ(c, &control_.back());
    TypeCheckFallThru(c);
    c->unreachable = false;

    consumer_.FallThruTo(this, c);
  }

  inline const Value<Consumer>& GetMergeValueFromStack(Control<Consumer>* c,
                                                       size_t i) {
    DCHECK_GT(c->merge.arity, i);
    DCHECK_GE(stack_.size(), c->merge.arity);
    return stack_[stack_.size() - c->merge.arity + i];
  }

  bool TypeCheckMergeValues(Control<Consumer>* c) {
    // Typecheck the values left on the stack.
    size_t avail = stack_.size() - c->stack_depth;
    size_t start = avail >= c->merge.arity ? 0 : c->merge.arity - avail;
    for (size_t i = start; i < c->merge.arity; ++i) {
      auto& val = GetMergeValueFromStack(c, i);
      auto& old = c->merge[i];
      if (val.type != old.type && val.type != kWasmVar) {
        this->errorf(
            this->pc_, "type error in merge[%zu] (expected %s, got %s)", i,
            WasmOpcodes::TypeName(old.type), WasmOpcodes::TypeName(val.type));
        return false;
      }
    }

    return true;
  }

  void TypeCheckFallThru(Control<Consumer>* c) {
    if (!do_validation) return;
    DCHECK_EQ(c, &control_.back());
    if (!do_validation) return;
    // Fallthru must match arity exactly.
    size_t expected = c->stack_depth + c->merge.arity;
    if (stack_.size() != expected &&
        (stack_.size() > expected || !c->unreachable)) {
      this->errorf(this->pc_,
                   "expected %u elements on the stack for fallthru to @%d",
                   c->merge.arity, startrel(c->pc));
      return;
    }

    TypeCheckMergeValues(c);
  }

  uint32_t EnvironmentCount() {
    return static_cast<uint32_t>(local_type_vec_.size());
  }

  virtual void onFirstError() {
    this->end_ = this->pc_;  // Terminate decoding loop.
    TRACE(" !%s\n", this->error_msg_.c_str());
  }

  inline wasm::WasmCodePosition position() {
    int offset = static_cast<int>(this->pc_ - this->start_);
    DCHECK_EQ(this->pc_ - this->start_, offset);  // overflows cannot happen
    return offset;
  }

  inline void BuildSimpleOperator(WasmOpcode opcode, FunctionSig* sig) {
    switch (sig->parameter_count()) {
      case 1: {
        auto val = Pop(0, sig->GetParam(0));
        auto* ret =
            sig->return_count() == 0 ? nullptr : Push(sig->GetReturn(0));
        consumer_.UnOp(this, opcode, sig, val, ret);
        break;
      }
      case 2: {
        auto rval = Pop(1, sig->GetParam(1));
        auto lval = Pop(0, sig->GetParam(0));
        auto* ret =
            sig->return_count() == 0 ? nullptr : Push(sig->GetReturn(0));
        consumer_.BinOp(this, opcode, sig, lval, rval, ret);
        break;
      }
      default:
        UNREACHABLE();
    }
  }
};

class EmptyConsumer {
 public:
  struct CValue {
    static CValue Unreachable() { return {}; }
    static CValue New() { return {}; }
  };
  struct CControl {
    static CControl Block() { return {}; }
    static CControl If() { return {}; }
    static CControl Loop() { return {}; }
    static CControl Try() { return {}; }
  };

#define DEFINE_EMPTY_CALLBACKS(name, ...) \
  IMPL(name, ##__VA_ARGS__) {}
  CONSUMER_FUNCTIONS(DEFINE_EMPTY_CALLBACKS)
#undef DEFINE_EMPTY_CALLBACKS
};

class WasmGraphBuildingConsumer : public EmptyConsumer {
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
        reinterpret_cast<SsaEnv*>(decoder->zone_->New(sizeof(SsaEnv)));
    uint32_t env_count = decoder->EnvironmentCount();
    size_t size = sizeof(TFNode*) * env_count;
    ssa_env->state = SsaEnv::kReached;
    ssa_env->locals =
        size > 0 ? reinterpret_cast<TFNode**>(decoder->zone_->New(size))
                 : nullptr;

    TFNode* start =
        builder_->Start(static_cast<int>(decoder->sig_->parameter_count() + 1));
    // Initialize local variables.
    uint32_t index = 0;
    for (; index < decoder->sig_->parameter_count(); ++index) {
      ssa_env->locals[index] = builder_->Param(index);
    }
    while (index < env_count) {
      ValueType type = decoder->local_type_vec_[index];
      TFNode* node = DefaultValue(type);
      while (index < env_count && decoder->local_type_vec_[index] == type) {
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
    SetEnv(Steal(decoder->zone_, break_env));
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
    SsaEnv* true_env = Steal(decoder->zone_, ssa_env_);
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
    SetEnv(Steal(decoder->zone_, ssa_env_));
  }

  IMPL(Loop, Control<Consumer>* block) {
    SsaEnv* finish_try_env = Steal(decoder->zone_, ssa_env_);
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

  IMPL(BrTable, const BranchTableOperand<do_validation>& operand,
       const Value<Consumer>& key) {
    SsaEnv* break_env = ssa_env_;
    // Build branches to the various blocks based on the table.
    TFNode* sw = BUILD(Switch, operand.table_count + 1, key.consumer_data.node);

    SsaEnv* copy = Steal(decoder->zone_, break_env);
    ssa_env_ = copy;
    BranchTableIterator<do_validation> iterator(decoder, operand);
    while (iterator.has_next()) {
      uint32_t i = iterator.cur_index();
      uint32_t target = iterator.next();
      ssa_env_ = Split(decoder, copy);
      ssa_env_->control = (i == operand.table_count) ? BUILD(IfDefault, sw)
                                                     : BUILD(IfValue, i, sw);
      BreakTo(decoder, decoder->GetControl(target));
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

  IMPL(Catch, const LocalIndexOperand<do_validation>& operand,
       Control<Consumer>* block) {
    DCHECK_NOT_NULL(block->consumer_data.try_info);
    current_catch_ = block->consumer_data.previous_catch;

    if (!ssa_env_->locals) return;  // unreachable

    TFNode* exception_as_i32 = BUILD(
        Catch, block->consumer_data.try_info->exception, decoder->position());
    ssa_env_->locals[operand.index] = exception_as_i32;
  }

 private:
  SsaEnv* ssa_env_;
  TFBuilder* builder_;
  int32_t current_catch_ = kNullCatch;

  IMPL_RET(bool, build) { return ssa_env_->go() && validate(decoder->ok()); }

  IMPL_RET(TryInfo*, current_try_info) {
    return decoder->control_[current_catch_].consumer_data.try_info;
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
    if (node == nullptr) {
      return nullptr;
    }

    const bool inside_try_scope = current_catch_ != kNullCatch;

    if (!inside_try_scope) {
      return node;
    }

    TFNode* if_success = nullptr;
    TFNode* if_exception = nullptr;
    if (!builder_->ThrowsException(node, &if_success, &if_exception)) {
      return node;
    }

    SsaEnv* success_env = Steal(decoder->zone_, ssa_env_);
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

    size_t avail =
        decoder->stack_.size() - decoder->control_.back().stack_depth;
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
        for (int i = decoder->EnvironmentCount() - 1; i >= 0; i--) {
          TFNode* a = to->locals[i];
          TFNode* b = from->locals[i];
          if (a != b) {
            TFNode* vals[] = {a, b};
            to->locals[i] =
                builder_->Phi(decoder->local_type_vec_[i], 2, vals, merge);
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
        for (int i = decoder->EnvironmentCount() - 1; i >= 0; i--) {
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
                builder_->Phi(decoder->local_type_vec_[i], count, vals, merge);
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
        decoder, decoder->pc_, static_cast<int>(decoder->total_locals()),
        decoder->zone_);
    if (decoder->failed()) return env;
    if (assigned != nullptr) {
      // Only introduce phis for variables assigned in this loop.
      for (int i = decoder->EnvironmentCount() - 1; i >= 0; i--) {
        if (!assigned->Contains(i)) continue;
        env->locals[i] = builder_->Phi(decoder->local_type_vec_[i], 1,
                                       &env->locals[i], env->control);
      }
      SsaEnv* loop_body_env = Split(decoder, env);
      builder_->StackCheck(decoder->position(), &(loop_body_env->effect),
                           &(loop_body_env->control));
      return loop_body_env;
    }

    // Conservatively introduce phis for all local variables.
    for (int i = decoder->EnvironmentCount() - 1; i >= 0; i--) {
      env->locals[i] = builder_->Phi(decoder->local_type_vec_[i], 1,
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
        reinterpret_cast<SsaEnv*>(decoder->zone_->New(sizeof(SsaEnv)));
    size_t size = sizeof(TFNode*) * decoder->EnvironmentCount();
    result->control = from->control;
    result->effect = from->effect;

    if (from->go()) {
      result->state = SsaEnv::kReached;
      result->locals =
          size > 0 ? reinterpret_cast<TFNode**>(decoder->zone_->New(size))
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
  WasmFullDecoder<true, EmptyConsumer> decoder(&zone, module, body, {});
  decoder.Decode();
  return decoder.toResult(nullptr);
}

DecodeResult BuildTFGraph(AccountingAllocator* allocator, TFBuilder* builder,
                          FunctionBody& body) {
  Zone zone(allocator, ZONE_NAME);
  const wasm::WasmModule* module =
      builder->module_env() ? builder->module_env()->module : nullptr;
  WasmFullDecoder<true, WasmGraphBuildingConsumer> decoder(
      &zone, module, body, WasmGraphBuildingConsumer(builder));
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
