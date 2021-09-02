// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/flags/flags.h"
#include "src/wasm/code-space-access.h"
#include "src/wasm/module-compiler.h"
#include "src/wasm/module-decoder.h"
#include "src/wasm/wasm-engine.h"
#include "src/wasm/wasm-features.h"
#include "src/wasm/wasm-opcodes.h"
#include "test/common/wasm/wasm-macro-gen.h"
#include "test/unittests/test-utils.h"

namespace v8 {
namespace internal {
namespace wasm {

enum MemoryProtectionMode {
  kNoProtection,
  kPku,
  kMprotect,
  kPkuWithMprotectFallback
};

std::string PrintMemoryProtectionTestParam(
    ::testing::TestParamInfo<MemoryProtectionMode> info) {
  switch (info.param) {
    case kNoProtection:
      return "NoProtection";
    case kPku:
      return "Pku";
    case kMprotect:
      return "Mprotect";
    case kPkuWithMprotectFallback:
      return "PkuWithMprotectFallback";
  }
}

class MemoryProtectionTest
    : public TestWithNativeContext,
      public ::testing::WithParamInterface<MemoryProtectionMode> {
 public:
  void SetUp() override {
    bool enable_pku =
        GetParam() == kPku || GetParam() == kPkuWithMprotectFallback;
    FLAG_wasm_memory_protection_keys = enable_pku;
    if (enable_pku) {
      GetWasmCodeManager()->InitializeMemoryProtectionKeyForTesting();
    }

    bool enable_mprotect =
        GetParam() == kMprotect || GetParam() == kPkuWithMprotectFallback;
    FLAG_wasm_write_protect_code_memory = enable_mprotect;

    native_module_ = CompileNativeModule();
    code_ = native_module_->GetCode(0);
  }

  void MakeCodeWritable() {
    native_module_->MakeWritable(base::AddressRegionOf(code_->instructions()));
  }

  void WriteToProtectedCode() {
    if (code_is_protected()) {
      ASSERT_DEATH_IF_SUPPORTED(code_->instructions()[0] = 0, "");
    } else {
      code_->instructions()[0] = 0;
    }
  }

  NativeModule* native_module() const { return native_module_.get(); }

  void WriteToUnprotectedCode() { code_->instructions()[0] = 0; }

 private:
  bool has_pku() {
    bool param_has_pku =
        GetParam() == kPku || GetParam() == kPkuWithMprotectFallback;
    return param_has_pku &&
           GetWasmCodeManager()->HasMemoryProtectionKeySupport();
  }

  bool has_mprotect() {
    return GetParam() == kMprotect || GetParam() == kPkuWithMprotectFallback;
  }

  bool code_is_protected() { return has_pku() || has_mprotect(); }

  std::shared_ptr<NativeModule> CompileNativeModule() {
    // Define the bytes for a module with a single empty function.
    static const byte module_bytes[] = {
        WASM_MODULE_HEADER, SECTION(Type, ENTRY_COUNT(1), SIG_ENTRY_v_v),
        SECTION(Function, ENTRY_COUNT(1), SIG_INDEX(0)),
        SECTION(Code, ENTRY_COUNT(1), ADD_COUNT(0 /* locals */, kExprEnd))};

    ModuleResult result =
        DecodeWasmModule(WasmFeatures::All(), std::begin(module_bytes),
                         std::end(module_bytes), false, kWasmOrigin,
                         isolate()->counters(), isolate()->metrics_recorder(),
                         v8::metrics::Recorder::ContextId::Empty(),
                         DecodingMethod::kSync, GetWasmEngine()->allocator());
    CHECK(result.ok());

    Handle<FixedArray> export_wrappers;
    ErrorThrower thrower(isolate(), "");
    constexpr int kNoCompilationId = 0;
    std::shared_ptr<NativeModule> native_module = CompileToNativeModule(
        isolate(), WasmFeatures::All(), &thrower, std::move(result).value(),
        ModuleWireBytes{base::ArrayVector(module_bytes)}, &export_wrappers,
        kNoCompilationId);
    CHECK(!thrower.error());
    CHECK_NOT_NULL(native_module);

    return native_module;
  }

  std::shared_ptr<NativeModule> native_module_;
  WasmCodeRefScope code_refs_;
  WasmCode* code_;
};

INSTANTIATE_TEST_SUITE_P(MemoryProtection, MemoryProtectionTest,
                         ::testing::Values(kNoProtection, kPku, kMprotect,
                                           kPkuWithMprotectFallback),
                         PrintMemoryProtectionTestParam);

TEST_P(MemoryProtectionTest, CodeNotWritableAfterCompilation) {
  WriteToProtectedCode();
}

TEST_P(MemoryProtectionTest, CodeWritableWithinScope) {
  CodeSpaceWriteScope write_scope(native_module());
  MakeCodeWritable();
  WriteToUnprotectedCode();
}

TEST_P(MemoryProtectionTest, CodeNotWritableAfterScope) {
  {
    CodeSpaceWriteScope write_scope(native_module());
    MakeCodeWritable();
    WriteToUnprotectedCode();
  }
  WriteToProtectedCode();
}

}  // namespace wasm
}  // namespace internal
}  // namespace v8
