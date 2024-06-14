// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/v8-context.h"
#include "include/v8-exception.h"
#include "include/v8-isolate.h"
#include "include/v8-local-handle.h"
#include "src/base/vector.h"
#include "src/execution/isolate.h"
#include "src/objects/property-descriptor.h"
#include "src/wasm/compilation-environment-inl.h"
#include "src/wasm/fuzzing/random-module-generation.h"
#include "src/wasm/wasm-engine.h"
#include "src/wasm/wasm-feature-flags.h"
#include "src/wasm/wasm-module.h"
#include "src/wasm/wasm-objects-inl.h"
#include "src/wasm/wasm-subtyping.h"
#include "src/zone/accounting-allocator.h"
#include "src/zone/zone.h"
#include "test/common/flag-utils.h"
#include "test/common/wasm/wasm-module-runner.h"
#include "test/fuzzer/fuzzer-support.h"
#include "test/fuzzer/wasm-fuzzer-common.h"

// This fuzzer fuzzes initializer expressions used e.g. in globals.
// The fuzzer creates a set of globals with initializer expressions and a set of
// functions containing the same body as these initializer expressions.
// The global value should be equal to the result of running the corresponding
// function.

namespace v8::internal::wasm::fuzzing {

namespace {

void FuzzIt(base::Vector<const uint8_t> data) {
  v8_fuzzer::FuzzerSupport* support = v8_fuzzer::FuzzerSupport::Get();
  v8::Isolate* isolate = support->GetIsolate();

  Isolate* i_isolate = reinterpret_cast<Isolate*>(isolate);
  v8::Isolate::Scope isolate_scope(isolate);

  // Clear recursive groups: The fuzzer creates random types in every run. These
  // are saved as recursive groups as part of the type canonicalizer, but types
  // from previous runs just waste memory.
  GetTypeCanonicalizer()->EmptyStorageForTesting();
  i_isolate->heap()->ClearWasmCanonicalRttsForTesting();

  v8::HandleScope handle_scope(isolate);
  v8::Context::Scope context_scope(support->GetContext());

  //  We switch it to synchronous mode to avoid the nondeterminism of background
  //  jobs finishing at random times.
  FlagScope<bool> sync_tier_up_scope(&v8_flags.wasm_sync_tier_up, true);
  // Enable the experimental features we want to fuzz. (Note that
  // EnableExperimentalWasmFeatures only enables staged features.)
  FlagScope<bool> deopt_scope(&v8_flags.wasm_deopt, true);
  FlagScope<bool> inlining_indirect(
      &v8_flags.experimental_wasm_inlining_call_indirect, true);
  // Make inlining more aggressive.
  FlagScope<bool> ignore_call_counts_scope(
      &v8_flags.wasm_inlining_ignore_call_counts, true);
  FlagScope<size_t> inlining_budget(&v8_flags.wasm_inlining_budget,
                                    v8_flags.wasm_inlining_budget * 5);
  FlagScope<size_t> inlining_size(&v8_flags.wasm_inlining_max_size,
                                  v8_flags.wasm_inlining_max_size * 5);
  FlagScope<size_t> inlining_factor(&v8_flags.wasm_inlining_factor,
                                    v8_flags.wasm_inlining_factor * 5);

  EnableExperimentalWasmFeatures(isolate);

  v8::TryCatch try_catch(isolate);
  HandleScope scope(i_isolate);
  AccountingAllocator allocator;
  Zone zone(&allocator, ZONE_NAME);

  base::Vector<const uint8_t> buffer = GenerateWasmModuleForDeopt(&zone, data);

  testing::SetupIsolateForWasmModule(i_isolate);
  ModuleWireBytes wire_bytes(buffer.begin(), buffer.end());
  auto enabled_features = WasmFeatures::FromIsolate(i_isolate);
  CompileTimeImports compile_imports = CompileTimeImports(
      {CompileTimeImport::kJsString, CompileTimeImport::kTextEncoder,
       CompileTimeImport::kTextDecoder});
  bool valid = GetWasmEngine()->SyncValidate(i_isolate, enabled_features,
                                             compile_imports, wire_bytes);

  if (v8_flags.wasm_fuzzer_gen_test) {
    GenerateTestCase(i_isolate, wire_bytes, valid);
  }

  FlagScope<bool> eager_compile(&v8_flags.wasm_lazy_compilation, false);
  ErrorThrower thrower(i_isolate, "WasmFuzzerSyncCompile");
  MaybeHandle<WasmModuleObject> compiled_module = GetWasmEngine()->SyncCompile(
      i_isolate, enabled_features, compile_imports, &thrower, wire_bytes);
  CHECK_EQ(valid, !compiled_module.is_null());
  CHECK_EQ(!valid, thrower.error());
  if (!valid) {
    FATAL("Invalid module: %s", thrower.error_msg());
  }
  thrower.Reset();
  CHECK(!i_isolate->has_exception());

  Handle<WasmModuleObject> module_object = compiled_module.ToHandleChecked();
  int32_t max_steps = kDefaultMaxFuzzerExecutedInstructions;
  int32_t nondeterminism = 0;
  CompileAllFunctionsForReferenceExecution(module_object->native_module(),
                                           &max_steps, &nondeterminism);
  Handle<WasmInstanceObject> instance =
      GetWasmEngine()
          ->SyncInstantiate(i_isolate, &thrower, module_object, {}, {})
          .ToHandleChecked();
  USE(instance);
}

}  // anonymous namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  FuzzIt({data, size});
  return 0;
}

}  // namespace v8::internal::wasm::fuzzing
