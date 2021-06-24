// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_WASM_INIT_EXPR_INTERFACE_H_
#define V8_WASM_INIT_EXPR_INTERFACE_H_

#include "src/execution/isolate.h"
#include "src/handles/handles.h"
#include "src/objects/oddball.h"
#include "src/wasm/decoder.h"
#include "src/wasm/function-body-decoder-impl.h"
#include "src/wasm/wasm-objects.h"
#include "src/wasm/wasm-value.h"

namespace v8 {
namespace internal {

template <typename T>
V8_INLINE Handle<T> handle(T object, Isolate* isolate);

namespace wasm {

// An interface for WasmFullDecoder used to decode initializer expressions. This
// interface has two modes: only validation (when {isolate_ == nullptr}), which
// is used in module-decoder, and code-generation (when {isolate_ != nullptr}),
// which is used in module-instantiate. We merge two distinct functionalities
// in one class to reduce the number of WasmFullDecoder instantiations, and thus
// V8 binary code size.
class InitExprInterface {
 public:
  static constexpr Decoder::ValidateFlag validate = Decoder::kFullValidation;
  static constexpr DecodingMode decoding_mode = kInitExpression;

  struct Value : public ValueBase<validate> {
    WasmValue runtime_value;

    template <typename... Args>
    explicit Value(Args&&... args) V8_NOEXCEPT
        : ValueBase(std::forward<Args>(args)...) {}
  };

  using Control = ControlBase<Value, validate>;
  using FullDecoder =
      WasmFullDecoder<validate, InitExprInterface, decoding_mode>;

  explicit InitExprInterface(const WasmModule* module, Isolate* isolate,
                             Handle<WasmInstanceObject> instance,
                             Handle<FixedArray> tagged_globals,
                             Handle<JSArrayBuffer> untagged_globals)
      : module_(module),
        outer_module_(nullptr),
        isolate_(isolate),
        instance_(instance),
        tagged_globals_(tagged_globals),
        untagged_globals_(untagged_globals) {
    DCHECK_NOT_NULL(isolate);
  }

  explicit InitExprInterface(WasmModule* outer_module)
      : module_(nullptr), outer_module_(outer_module), isolate_(nullptr) {}

#define EMPTY_INTERFACE_FUNCTION(name, ...) \
  V8_INLINE void name(FullDecoder* decoder, ##__VA_ARGS__) {}
  INTERFACE_META_FUNCTIONS(EMPTY_INTERFACE_FUNCTION)
  INTERFACE_NON_CONSTANT_FUNCTIONS(EMPTY_INTERFACE_FUNCTION)
#undef EMPTY_INTERFACE_FUNCTION

  void I32Const(FullDecoder* decoder, Value* result, int32_t value) {
    if (isolate_ != nullptr) result->runtime_value = WasmValue(value);
  }
  void I64Const(FullDecoder* decoder, Value* result, int64_t value) {
    if (isolate_ != nullptr) result->runtime_value = WasmValue(value);
  }
  void F32Const(FullDecoder* decoder, Value* result, float value) {
    if (isolate_ != nullptr) result->runtime_value = WasmValue(value);
  }
  void F64Const(FullDecoder* decoder, Value* result, double value) {
    if (isolate_) result->runtime_value = WasmValue(value);
  }
  void S128Const(FullDecoder* decoder, Simd128Immediate<validate>& imm,
                 Value* result) {
    if (isolate_ != nullptr) {
      result->runtime_value = WasmValue(imm.value, kWasmS128);
    }
  }
  void RefNull(FullDecoder* decoder, ValueType type, Value* result) {
    if (isolate_ != nullptr) {
      result->runtime_value = WasmValue(
          handle(ReadOnlyRoots(isolate_).null_value(), isolate_), type);
    }
  }
  void RefFunc(FullDecoder* decoder, uint32_t function_index, Value* result) {
    if (isolate_ != nullptr) {
      auto function = WasmInstanceObject::GetOrCreateWasmExternalFunction(
          isolate_, instance_, function_index);
      result->runtime_value = WasmValue(
          function, ValueType::Ref(module_->functions[function_index].sig_index,
                                   kNonNullable));
    } else {
      outer_module_->functions[function_index].declared = true;
    }
  }
  void GlobalGet(FullDecoder* decoder, Value* result,
                 const GlobalIndexImmediate<validate>& imm) {
    if (isolate_ != nullptr) {
      const WasmGlobal& global = module_->globals[imm.index];
      result->runtime_value =
          global.type.is_numeric()
              ? WasmValue(GetRawUntaggedGlobalPtr(global), global.type)
              : WasmValue(handle(tagged_globals_->get(global.offset), isolate_),
                          global.type);
    }
  }
  void StructNewWithRtt(FullDecoder* decoder,
                        const StructIndexImmediate<validate>& imm,
                        const Value& rtt, const Value args[], Value* result) {
    if (isolate_ != nullptr) {
      std::vector<WasmValue> field_values(imm.struct_type->field_count());
      for (size_t i = 0; i < field_values.size(); i++) {
        field_values[i] = args[i].runtime_value;
      }
      result->runtime_value =
          WasmValue(isolate_->factory()->NewWasmStruct(
                        imm.struct_type, field_values.data(),
                        Handle<Map>::cast(rtt.runtime_value.to_ref())),
                    ValueType::Ref(HeapType(imm.index), kNonNullable));
    }
  }
  void ArrayInit(FullDecoder* decoder, const ArrayIndexImmediate<validate>& imm,
                 const base::Vector<Value>& elements, const Value& rtt,
                 Value* result) {
    if (isolate_ != nullptr) {
      std::vector<WasmValue> element_values;
      for (Value elem : elements) element_values.push_back(elem.runtime_value);
      result->runtime_value =
          WasmValue(isolate_->factory()->NewWasmArray(
                        imm.array_type, element_values,
                        Handle<Map>::cast(rtt.runtime_value.to_ref())),
                    ValueType::Ref(HeapType(imm.index), kNonNullable));
    }
  }
  void RttCanon(FullDecoder* decoder, uint32_t type_index, Value* result) {
    if (isolate_ != nullptr) {
      result->runtime_value = WasmValue(
          handle(instance_->managed_object_maps().get(type_index), isolate_),
          ValueType::Rtt(type_index, 0));
    }
  }
  void RttSub(FullDecoder* decoder, uint32_t type_index, const Value& parent,
              Value* result, WasmRttSubMode mode) {
    if (isolate_ != nullptr) {
      ValueType type = parent.type.has_depth()
                           ? ValueType::Rtt(type_index, parent.type.depth() + 1)
                           : ValueType::Rtt(type_index);
      result->runtime_value = WasmValue(
          Handle<Object>::cast(AllocateSubRtt(
              isolate_, instance_, type_index,
              Handle<Map>::cast(parent.runtime_value.to_ref()), mode)),
          type);
    }
  }
  void DoReturn(FullDecoder* decoder, uint32_t /*drop_values*/) {
    end_found_ = true;
    // End decoding on "end".
    decoder->set_end(decoder->pc() + 1);
    if (isolate_ != nullptr) result_ = decoder->stack_value(1)->runtime_value;
  }

  WasmValue result() {
    DCHECK_NOT_NULL(isolate_);
    return result_;
  }
  bool end_found() { return end_found_; }

 private:
  byte* GetRawUntaggedGlobalPtr(const WasmGlobal& global) {
    return reinterpret_cast<byte*>(untagged_globals_->backing_store()) +
           global.offset;
  }

  bool end_found_ = false;
  WasmValue result_;
  const WasmModule* module_;
  WasmModule* outer_module_;
  Isolate* isolate_;
  Handle<WasmInstanceObject> instance_;
  Handle<FixedArray> tagged_globals_;
  Handle<JSArrayBuffer> untagged_globals_;
};

}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_WASM_INIT_EXPR_INTERFACE_H_
