// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/wasm-code-specialization.h"

#include "src/assembler-inl.h"
#include "src/base/optional.h"
#include "src/objects-inl.h"
#include "src/source-position-table.h"
#include "src/wasm/decoder.h"
#include "src/wasm/wasm-code-manager.h"
#include "src/wasm/wasm-module.h"
#include "src/wasm/wasm-objects-inl.h"

namespace v8 {
namespace internal {
namespace wasm {

uint32_t ExtractDirectCallIndex(wasm::Decoder& decoder, const byte* pc) {
  DCHECK_EQ(static_cast<int>(kExprCallFunction), static_cast<int>(*pc));
  decoder.Reset(pc + 1, pc + 6);
  uint32_t call_idx = decoder.consume_u32v("call index");
  DCHECK(decoder.ok());
  DCHECK_GE(kMaxInt, call_idx);
  return call_idx;
}

namespace {

int AdvanceSourcePositionTableIterator(SourcePositionTableIterator& iterator,
                                       size_t offset_l) {
  DCHECK_GE(kMaxInt, offset_l);
  int offset = static_cast<int>(offset_l);
  DCHECK(!iterator.done());
  int byte_pos;
  do {
    byte_pos = iterator.source_position().ScriptOffset();
    iterator.Advance();
  } while (!iterator.done() && iterator.code_offset() <= offset);
  return byte_pos;
}

class PatchDirectCallsHelper {
 public:
  PatchDirectCallsHelper(NativeModule* native_module, const WasmCode* code)
      : source_pos_it(ByteArray::cast(
            native_module->compiled_module()->source_positions()->get(
                static_cast<int>(code->index())))),
        decoder(nullptr, nullptr) {
    uint32_t func_index = code->index();
    WasmCompiledModule* comp_mod = native_module->compiled_module();
    func_bytes =
        comp_mod->shared()->module_bytes()->GetChars() +
        comp_mod->shared()->module()->functions[func_index].code.offset();
  }

  PatchDirectCallsHelper(NativeModule* native_module, Code* code)
      : source_pos_it(code->SourcePositionTable()), decoder(nullptr, nullptr) {
    FixedArray* deopt_data = code->deoptimization_data();
    DCHECK_EQ(2, deopt_data->length());
    WasmSharedModuleData* shared = native_module->compiled_module()->shared();
    int func_index = Smi::ToInt(deopt_data->get(1));
    func_bytes = shared->module_bytes()->GetChars() +
                 shared->module()->functions[func_index].code.offset();
  }

  SourcePositionTableIterator source_pos_it;
  Decoder decoder;
  const byte* func_bytes;
};

}  // namespace

CodeSpecialization::CodeSpecialization(Isolate* isolate, Zone* zone) {}

CodeSpecialization::~CodeSpecialization() {}

void CodeSpecialization::UpdateInstanceReferences(
    Handle<HeapObject> old_instance_placeholder,
    Handle<HeapObject> new_instance_placeholder) {
  DCHECK(!old_instance_placeholder.is_null());
  DCHECK(!new_instance_placeholder.is_null());
  old_instance_placeholder_ = old_instance_placeholder;
  new_instance_placeholder_ = new_instance_placeholder;
}

void CodeSpecialization::RelocateDirectCalls(NativeModule* native_module) {
  DCHECK_NULL(relocate_direct_calls_module_);
  DCHECK_NOT_NULL(native_module);
  relocate_direct_calls_module_ = native_module;
}

void CodeSpecialization::RelocatePointer(Address old_ptr, Address new_ptr) {
  DCHECK_EQ(0, pointers_to_relocate_.count(old_ptr));
  DCHECK_EQ(0, pointers_to_relocate_.count(new_ptr));
  pointers_to_relocate_.insert(std::make_pair(old_ptr, new_ptr));
}

bool CodeSpecialization::ApplyToWholeModule(NativeModule* native_module,
                                            ICacheFlushMode icache_flush_mode) {
  DisallowHeapAllocation no_gc;
  WasmCompiledModule* compiled_module = native_module->compiled_module();
  WasmSharedModuleData* shared = compiled_module->shared();
  WasmModule* module = shared->module();
  std::vector<WasmFunction>* wasm_functions = &shared->module()->functions;
  DCHECK_EQ(compiled_module->export_wrappers()->length(),
            shared->module()->num_exported_functions);

  bool changed = false;
  int func_index = module->num_imported_functions;

  // Patch all wasm functions.
  for (int num_wasm_functions = static_cast<int>(wasm_functions->size());
       func_index < num_wasm_functions; ++func_index) {
    WasmCode* wasm_function = native_module->GetCode(func_index);
    // TODO(clemensh): Get rid of this nullptr check
    if (wasm_function == nullptr ||
        wasm_function->kind() != WasmCode::kFunction) {
      continue;
    }
    changed |= ApplyToWasmCode(wasm_function, icache_flush_mode);
  }

  bool patch_wasm_instance_placeholders =
      !old_instance_placeholder_.is_identical_to(new_instance_placeholder_);

  // Patch all exported functions (JS_TO_WASM_FUNCTION).
  int reloc_mode = 0;
  // Patch CODE_TARGET if we shall relocate direct calls. If we patch direct
  // calls, the instance registered for that (relocate_direct_calls_module_)
  // should match the instance we currently patch (instance).
  if (relocate_direct_calls_module_ != nullptr) {
    DCHECK_EQ(native_module, relocate_direct_calls_module_);
    reloc_mode |= RelocInfo::ModeMask(RelocInfo::JS_TO_WASM_CALL);
  }
  // Instance references are simply embedded objects.
  if (patch_wasm_instance_placeholders) {
    reloc_mode |= RelocInfo::ModeMask(RelocInfo::EMBEDDED_OBJECT);
  }
  if (!reloc_mode) return changed;
  int wrapper_index = 0;
  for (auto exp : module->export_table) {
    if (exp.kind != kExternalFunction) continue;
    Code* export_wrapper =
        Code::cast(compiled_module->export_wrappers()->get(wrapper_index++));
    if (export_wrapper->kind() != Code::JS_TO_WASM_FUNCTION) continue;
    for (RelocIterator it(export_wrapper, reloc_mode); !it.done(); it.next()) {
      RelocInfo::Mode mode = it.rinfo()->rmode();
      switch (mode) {
        case RelocInfo::JS_TO_WASM_CALL: {
          const WasmCode* new_code = native_module->GetCode(exp.index);
          it.rinfo()->set_js_to_wasm_address(new_code->instructions().start(),
                                             icache_flush_mode);
        } break;
        case RelocInfo::EMBEDDED_OBJECT: {
          const HeapObject* old = it.rinfo()->target_object();
          if (*old_instance_placeholder_ == old) {
            it.rinfo()->set_target_object(
                *new_instance_placeholder_,
                WriteBarrierMode::UPDATE_WRITE_BARRIER, icache_flush_mode);
          }
        } break;
        default:
          UNREACHABLE();
      }
    }
    changed = true;
  }
  DCHECK_EQ(module->functions.size(), func_index);
  DCHECK_EQ(compiled_module->export_wrappers()->length(), wrapper_index);
  return changed;
}

bool CodeSpecialization::ApplyToWasmCode(wasm::WasmCode* code,
                                         ICacheFlushMode icache_flush_mode) {
  DisallowHeapAllocation no_gc;
  DCHECK_EQ(wasm::WasmCode::kFunction, code->kind());

  bool reloc_direct_calls = relocate_direct_calls_module_ != nullptr;
  bool reloc_pointers = pointers_to_relocate_.size() > 0;

  int reloc_mode = 0;
  auto add_mode = [&reloc_mode](bool cond, RelocInfo::Mode mode) {
    if (cond) reloc_mode |= RelocInfo::ModeMask(mode);
  };
  add_mode(reloc_direct_calls, RelocInfo::WASM_CALL);
  add_mode(reloc_pointers, RelocInfo::WASM_GLOBAL_HANDLE);

  base::Optional<PatchDirectCallsHelper> patch_direct_calls_helper;
  bool changed = false;

  NativeModule* native_module = code->native_module();

  RelocIterator it(code->instructions(), code->reloc_info(),
                   code->constant_pool(), reloc_mode);
  for (; !it.done(); it.next()) {
    RelocInfo::Mode mode = it.rinfo()->rmode();
    switch (mode) {
      case RelocInfo::WASM_CALL: {
        DCHECK(reloc_direct_calls);
        // Iterate simultaneously over the relocation information and the source
        // position table. For each call in the reloc info, move the source
        // position iterator forward to that position to find the byte offset of
        // the respective call. Then extract the call index from the module wire
        // bytes to find the new compiled function.
        size_t offset = it.rinfo()->pc() - code->instructions().start();
        if (!patch_direct_calls_helper) {
          patch_direct_calls_helper.emplace(relocate_direct_calls_module_,
                                            code);
        }
        int byte_pos = AdvanceSourcePositionTableIterator(
            patch_direct_calls_helper->source_pos_it, offset);
        uint32_t called_func_index = ExtractDirectCallIndex(
            patch_direct_calls_helper->decoder,
            patch_direct_calls_helper->func_bytes + byte_pos);
        const WasmCode* new_code = native_module->GetCode(called_func_index);
        it.rinfo()->set_wasm_call_address(new_code->instructions().start(),
                                          icache_flush_mode);
        changed = true;
      } break;
      case RelocInfo::WASM_GLOBAL_HANDLE: {
        DCHECK(reloc_pointers);
        Address old_ptr = it.rinfo()->global_handle();
        auto entry = pointers_to_relocate_.find(old_ptr);
        if (entry != pointers_to_relocate_.end()) {
          Address new_ptr = entry->second;
          it.rinfo()->set_global_handle(new_ptr, icache_flush_mode);
          changed = true;
        }
      } break;
      default:
        UNREACHABLE();
    }
  }

  return changed;
}

}  // namespace wasm
}  // namespace internal
}  // namespace v8
