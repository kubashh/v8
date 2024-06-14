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
#include "src/wasm/module-compiler.h"
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

  std::vector<std::string> callees;
  base::Vector<const uint8_t> buffer =
      GenerateWasmModuleForDeopt(&zone, data, callees);

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

  auto arguments = base::OwnedVector<Handle<Object>>::New(1);

  for (const std::string& callee_name : callees) {
    Handle<WasmExportedFunction> callee =
        testing::GetExportedFunction(i_isolate, instance, callee_name.c_str())
            .ToHandleChecked();

    struct OomCallbackData {
      Isolate* isolate;
      bool heap_limit_reached{false};
      size_t initial_limit{0};
    } oom_callback_data{i_isolate};
    auto heap_limit_callback = [](void* raw_data, size_t current_limit,
                                  size_t initial_limit) -> size_t {
      OomCallbackData* data = reinterpret_cast<OomCallbackData*>(raw_data);
      data->heap_limit_reached = true;
      data->isolate->TerminateExecution();
      data->initial_limit = initial_limit;
      // Return a slightly raised limit, just to make it to the next
      // interrupt check point, where execution will terminate.
      return initial_limit * 1.25;
    };
    i_isolate->heap()->AddNearHeapLimitCallback(heap_limit_callback,
                                                &oom_callback_data);

    arguments[0] = callee;
    std::unique_ptr<const char[]> exception;
    int32_t result = testing::CallWasmFunctionForTesting(
        i_isolate, instance, "main", arguments.as_vector(), &exception);
    std::cerr << "result: " << result << std::endl;
    bool execute = true;
    // Reached max steps, do not try to execute the test module as it might
    // never terminate.
    if (max_steps < 0) execute = false;
    // If there is nondeterminism, we cannot guarantee the behavior of the test
    // module, and in particular it may not terminate.
    if (nondeterminism != 0) execute = false;
    // Similar to max steps reached, also discard modules that need too much
    // memory.
    i_isolate->heap()->RemoveNearHeapLimitCallback(
        heap_limit_callback, oom_callback_data.initial_limit);
    if (oom_callback_data.heap_limit_reached) {
      std::cerr << "heap limit reached" << std::endl;
      execute = false;
      isolate->CancelTerminateExecution();
    }

    if (exception) {
      std::cerr << "exception: " << exception.get() << std::endl;
      if (strcmp(exception.get(),
                 "RangeError: Maximum call stack size exceeded") == 0) {
        // There was a stack overflow, which may happen nondeterministically. We
        // cannot guarantee the behavior of the test module, and in particular
        // it may not terminate.
        execute = false;
      }
    }
    if (!execute) {
      // Before discarding the module, see if Turbofan runs into any DCHECKs.
      TierUpAllForTesting(i_isolate, instance->trusted_data(i_isolate));
      return;
    }

    // TODO(mliedtke): The plan was to tier-up, run with turbofan and then run
    // with the next call target but we do not know if the next callee triggers
    // non-determinsm etc.
    // Better plan is to do all the stuff on the reference execution and then
    // repeat it with liftoff + Turbofan for the actual deopt testing.
  }
}

}  // anonymous namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  FuzzIt({data, size});
  return 0;
}

}  // namespace v8::internal::wasm::fuzzing
