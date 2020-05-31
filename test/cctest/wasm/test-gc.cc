// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>

#include "src/utils/utils.h"
#include "src/utils/vector.h"
#include "src/wasm/module-decoder.h"
#include "src/wasm/struct-types.h"
#include "src/wasm/wasm-engine.h"
#include "src/wasm/wasm-module-builder.h"
#include "src/wasm/wasm-module.h"
#include "src/wasm/wasm-objects-inl.h"
#include "src/wasm/wasm-opcodes.h"
#include "test/cctest/cctest.h"
#include "test/cctest/compiler/value-helper.h"
#include "test/cctest/wasm/wasm-run-utils.h"
#include "test/common/wasm/test-signatures.h"
#include "test/common/wasm/wasm-macro-gen.h"
#include "test/common/wasm/wasm-module-runner.h"

namespace v8 {
namespace internal {
namespace wasm {
namespace test_gc {

using ExecEnv =
    std::tuple<Isolate*, HandleScope, Handle<WasmInstanceObject>, ErrorThrower>;
Isolate* isolate(ExecEnv* env) { return std::get<0>(*env); }
Handle<WasmInstanceObject> instance(ExecEnv* env) { return std::get<2>(*env); }
ErrorThrower* thrower(ExecEnv* env) { return &std::get<3>(*env); }

using F = std::pair<ValueType, bool>;

#define WASM_GC_TEST_HEADER                                  \
  /* TODO(7748): Implement support in other tiers. */        \
  if (execution_tier == ExecutionTier::kLiftoff) return;     \
  if (execution_tier == ExecutionTier::kInterpreter) return; \
  TestSignatures sigs;                                       \
  EXPERIMENTAL_FLAG_SCOPE(gc);                               \
  EXPERIMENTAL_FLAG_SCOPE(typed_funcref);                    \
  EXPERIMENTAL_FLAG_SCOPE(anyref);                           \
  v8::internal::AccountingAllocator allocator;               \
  Zone zone(&allocator, ZONE_NAME);                          \
  WasmModuleBuilder* builder = new (&zone) WasmModuleBuilder(&zone);

void DefineFun(WasmModuleBuilder* builder, const char* name, FunctionSig* sig,
               std::initializer_list<ValueType> locals,
               std::initializer_list<byte> code) {
  WasmFunctionBuilder* fun = builder->AddFunction(sig);
  fun->builder()->AddExport(CStrVector(name), fun);
  for (ValueType local : locals) {
    fun->AddLocal(local);
  }
  fun->EmitCode(code.begin(), static_cast<uint32_t>(code.size()));
}

uint32_t DefineStruct(WasmModuleBuilder* builder,
                      std::initializer_list<F> fields) {
  StructType::Builder type_builder(builder->zone(),
                                   static_cast<uint32_t>(fields.size()));
  for (F field : fields) {
    type_builder.AddField(field.first, field.second);
  }
  return builder->AddStructType(type_builder.Build());
}

ExecEnv CompileModule(WasmModuleBuilder* builder, Zone* zone) {
  ZoneBuffer buffer(zone);
  builder->WriteTo(&buffer);
  Isolate* isolate = CcTest::InitIsolateOnce();
  HandleScope scope(isolate);
  testing::SetupIsolateForWasmModule(isolate);
  ErrorThrower thrower(isolate, "Test");
  MaybeHandle<WasmInstanceObject> maybe_instance =
      testing::CompileAndInstantiateForTesting(
          isolate, &thrower, ModuleWireBytes(buffer.begin(), buffer.end()));
  if (thrower.error()) FATAL("%s", thrower.error_msg());
  Handle<WasmInstanceObject> instance = maybe_instance.ToHandleChecked();
  return std::make_tuple(isolate, std::move(scope), instance,
                         std::move(thrower));
}

void CheckResult(ExecEnv* env, const char* function, int32_t expected,
                 std::initializer_list<Object> args) {
  Handle<Object>* argv = new Handle<Object>[args.size()];
  int i = 0;
  for (Object arg : args) {
    argv[i++] = handle(arg, isolate(env));
  }
  CHECK_EQ(expected, testing::CallWasmFunctionForTesting(
                         isolate(env), instance(env), thrower(env), function,
                         static_cast<uint32_t>(args.size()), argv));
  delete[] argv;
}

MaybeHandle<Object> GetJSResult(ExecEnv* env, const char* function,
                                std::initializer_list<Object> args) {
  Handle<Object>* argv = new Handle<Object>[args.size()];
  Isolate* isol = isolate(env);
  int i = 0;
  for (Object arg : args) {
    argv[i++] = handle(arg, isol);
  }
  Handle<WasmExportedFunction> exported =
      testing::GetExportedFunction(isol, instance(env), function)
          .ToHandleChecked();
  MaybeHandle<Object> result =
      Execution::Call(isol, exported, isol->factory()->undefined_value(),
                      static_cast<uint32_t>(args.size()), argv);
  delete[] argv;
  return result;
}

WASM_EXEC_TEST(BasicStruct) {
  WASM_GC_TEST_HEADER
  uint32_t type_index =
      DefineStruct(builder, {F(kWasmI32, true), F(kWasmI32, true)});
  ValueType kRefTypes[] = {ValueType(ValueType::kRef, type_index)};
  ValueType kOptRefType = ValueType(ValueType::kOptRef, type_index);
  FunctionSig sig_q_v(1, 0, kRefTypes);

  // Test struct.new and struct.get.

  DefineFun(builder, "f", sigs.i_v(), {},
            {WASM_STRUCT_GET(
                 type_index, 0,
                 WASM_STRUCT_NEW(type_index, WASM_I32V(42), WASM_I32V(64))),
             kExprEnd});

  // Test struct.new and struct.get.
  DefineFun(builder, "g", sigs.i_v(), {},
            {WASM_STRUCT_GET(
                 type_index, 1,
                 WASM_STRUCT_NEW(type_index, WASM_I32V(42), WASM_I32V(64))),
             kExprEnd});

  // Test struct.new, returning struct references to JS.
  DefineFun(
      builder, "h", &sig_q_v, {},
      {WASM_STRUCT_NEW(type_index, WASM_I32V(42), WASM_I32V(64)), kExprEnd});

  // Test struct.set, struct refs types in locals.
  uint32_t j_local_index = 0;
  uint32_t j_field_index = 0;
  DefineFun(
      builder, "j", sigs.i_v(), {kOptRefType},
      {WASM_SET_LOCAL(j_local_index, WASM_STRUCT_NEW(type_index, WASM_I32V(42),
                                                     WASM_I32V(64))),
       WASM_STRUCT_SET(type_index, j_field_index, WASM_GET_LOCAL(j_local_index),
                       WASM_I32V(-99)),
       WASM_STRUCT_GET(type_index, j_field_index,
                       WASM_GET_LOCAL(j_local_index)),
       kExprEnd});

  // Test struct.set, ref.as_non_null,
  // struct refs types in globals and if-results.
  uint32_t k_global_index = builder->AddGlobal(kOptRefType);
  uint32_t k_field_index = 0;
  DefineFun(
      builder, "k", sigs.i_v(), {},
      {WASM_SET_GLOBAL(
           k_global_index,
           WASM_STRUCT_NEW(type_index, WASM_I32V(55), WASM_I32V(66))),
       WASM_STRUCT_GET(type_index, k_field_index,
                       WASM_REF_AS_NON_NULL(WASM_IF_ELSE_R(
                           kOptRefType, WASM_I32V(1),
                           WASM_GET_GLOBAL(k_global_index), WASM_REF_NULL))),
       kExprEnd});

  // Test br_on_null 1.
  uint32_t l_local_index = 0;
  DefineFun(builder, "l", sigs.i_v(), {kOptRefType},
            {WASM_BLOCK_I(WASM_I32V(42),
                          // Branch will be taken.
                          // 42 left on stack outside the block (not 52).
                          WASM_BR_ON_NULL(0, WASM_GET_LOCAL(l_local_index)),
                          WASM_I32V(52), WASM_BR(0)),
             kExprEnd});

  // Test br_on_null 2.
  uint32_t m_field_index = 0;
  DefineFun(builder, "m", sigs.i_v(), {},
            {WASM_BLOCK_I(WASM_I32V(42),
                          WASM_STRUCT_GET(
                              type_index, m_field_index,
                              // Branch will not be taken.
                              // 52 left on stack outside the block (not 42).
                              WASM_BR_ON_NULL(
                                  0, WASM_STRUCT_NEW(type_index, WASM_I32V(52),
                                                     WASM_I32V(62)))),
                          WASM_BR(0)),
             kExprEnd});

  // Test ref.eq
  uint32_t n_local_index = 0;
  DefineFun(
      builder, "n", sigs.i_v(), {kOptRefType},
      {WASM_SET_LOCAL(n_local_index, WASM_STRUCT_NEW(type_index, WASM_I32V(55),
                                                     WASM_I32V(66))),
       WASM_I32_ADD(
           WASM_I32_SHL(WASM_REF_EQ(  // true
                            WASM_GET_LOCAL(n_local_index),
                            WASM_GET_LOCAL(n_local_index)),
                        WASM_I32V(0)),
           WASM_I32_ADD(
               WASM_I32_SHL(WASM_REF_EQ(  // false
                                WASM_GET_LOCAL(n_local_index),
                                WASM_STRUCT_NEW(type_index, WASM_I32V(55),
                                                WASM_I32V(66))),
                            WASM_I32V(1)),
               WASM_I32_ADD(WASM_I32_SHL(  // false
                                WASM_REF_EQ(WASM_GET_LOCAL(n_local_index),
                                            WASM_REF_NULL),
                                WASM_I32V(2)),
                            WASM_I32_SHL(WASM_REF_EQ(  // true
                                             WASM_REF_NULL, WASM_REF_NULL),
                                         WASM_I32V(3))))),
       kExprEnd});
  // Result: 0b1001

  /************************* End of test definitions *************************/
  ExecEnv env = CompileModule(builder, &zone);

  CheckResult(&env, "f", 42, {});
  CheckResult(&env, "g", 64, {});

  // TODO(7748): This uses the JavaScript interface to retrieve the plain
  // WasmStruct. Once the JS interaction story is settled, this may well
  // need to be changed.
  MaybeHandle<Object> h_result = GetJSResult(&env, "h", {});
  CHECK(h_result.ToHandleChecked()->IsWasmStruct());

  CheckResult(&env, "j", -99, {});
  CheckResult(&env, "k", 55, {});
  CheckResult(&env, "l", 42, {});
  CheckResult(&env, "m", 52, {});
  CheckResult(&env, "n", 0b1001, {});
}

WASM_EXEC_TEST(LetInstruction) {
  WASM_GC_TEST_HEADER
  uint32_t type_index =
      DefineStruct(builder, {F(kWasmI32, true), F(kWasmI32, true)});

  uint32_t let_local_index = 0;
  uint32_t let_field_index = 0;
  DefineFun(
      builder, "let_test_1", sigs.i_v(), {},
      {WASM_LET_1_I(WASM_REF_TYPE(type_index),
                    WASM_STRUCT_NEW(type_index, WASM_I32V(42), WASM_I32V(52)),
                    WASM_STRUCT_GET(type_index, let_field_index,
                                    WASM_GET_LOCAL(let_local_index))),
       kExprEnd});

  uint32_t let_2_field_index = 0;
  DefineFun(
      builder, "let_test_2", sigs.i_v(), {},
      {WASM_LET_2_I(kLocalI32, WASM_I32_ADD(WASM_I32V(42), WASM_I32V(-32)),
                    WASM_REF_TYPE(type_index),
                    WASM_STRUCT_NEW(type_index, WASM_I32V(42), WASM_I32V(52)),
                    WASM_I32_MUL(WASM_STRUCT_GET(type_index, let_2_field_index,
                                                 WASM_GET_LOCAL(1)),
                                 WASM_GET_LOCAL(0))),
       kExprEnd});

  DefineFun(
      builder, "let_test_locals", sigs.i_i(), {kWasmI32},
      {WASM_SET_LOCAL(1, WASM_I32V(100)),
       WASM_LET_2_I(
           kLocalI32, WASM_I32V(1), kLocalI32, WASM_I32V(10),
           WASM_I32_SUB(WASM_I32_ADD(WASM_GET_LOCAL(0),     // 1st let-local
                                     WASM_GET_LOCAL(2)),    // Parameter
                        WASM_I32_ADD(WASM_GET_LOCAL(1),     // 2nd let-local
                                     WASM_GET_LOCAL(3)))),  // Function local
       kExprEnd});
  // Result: (1 + 1000) - (10 + 100) = 891

  uint32_t let_erase_local_index = 0;
  DefineFun(builder, "let_test_erase", sigs.i_v(), {kWasmI32},
            {WASM_SET_LOCAL(let_erase_local_index, WASM_I32V(0)),
             WASM_LET_1_V(kLocalI32, WASM_I32V(1), WASM_NOP),
             WASM_GET_LOCAL(let_erase_local_index), kExprEnd});
  // The result should be 0 and not 1, as local_get(0) refers to the original
  // local.

  ExecEnv env = CompileModule(builder, &zone);

  CheckResult(&env, "let_test_1", 42, {});
  CheckResult(&env, "let_test_2", 420, {});
  CheckResult(&env, "let_test_locals", 891, {Smi::FromInt(1000)});
  CheckResult(&env, "let_test_erase", 0, {});
}

WASM_EXEC_TEST(BasicArray) {
  WASM_GC_TEST_HEADER

  ArrayType type(wasm::kWasmI32, true);
  uint32_t type_index = builder->AddArrayType(&type);
  ValueType kRefTypes[] = {ValueType(ValueType::kRef, type_index)};
  FunctionSig sig_q_v(1, 0, kRefTypes);
  ValueType kOptRefType = ValueType(ValueType::kOptRef, type_index);

  // f: a = [12, 12, 12]; a[1] = 42; return a[arg0]
  uint32_t local_index = 1;
  DefineFun(
      builder, "f", sigs.i_i(), {kOptRefType},
      {WASM_SET_LOCAL(local_index,
                      WASM_ARRAY_NEW(type_index, WASM_I32V(12), WASM_I32V(3))),
       WASM_ARRAY_SET(type_index, WASM_GET_LOCAL(local_index), WASM_I32V(1),
                      WASM_I32V(42)),
       WASM_ARRAY_GET(type_index, WASM_GET_LOCAL(local_index),
                      WASM_GET_LOCAL(0)),
       kExprEnd});

  // Reads and returns an array's length.
  DefineFun(builder, "g", sigs.i_v(), {},
            {WASM_ARRAY_LEN(type_index, WASM_ARRAY_NEW(type_index, WASM_I32V(0),
                                                       WASM_I32V(42))),
             kExprEnd});

  // Create an array of length 2, initialized to [42, 42].
  DefineFun(
      builder, "h", &sig_q_v, {},
      {WASM_ARRAY_NEW(type_index, WASM_I32V(42), WASM_I32V(2)), kExprEnd});

  ExecEnv env = CompileModule(builder, &zone);

  CheckResult(&env, "f", 12, {Smi::FromInt(0)});
  CheckResult(&env, "f", 42, {Smi::FromInt(1)});
  CheckResult(&env, "f", 12, {Smi::FromInt(2)});

  Isolate* isolate = std::get<0>(env);

  TryCatch try_catch(reinterpret_cast<v8::Isolate*>(isolate));

  MaybeHandle<Object> f_result_1 = GetJSResult(&env, "f", {Smi::FromInt(3)});
  CHECK(f_result_1.is_null());
  CHECK(try_catch.HasCaught());
  isolate->clear_pending_exception();

  MaybeHandle<Object> f_result_2 = GetJSResult(&env, "f", {Smi::FromInt(-1)});
  CHECK(f_result_2.is_null());
  CHECK(try_catch.HasCaught());
  isolate->clear_pending_exception();

  CheckResult(&env, "g", 42, {});

  // TODO(7748): This uses the JavaScript interface to retrieve the plain
  // WasmArray. Once the JS interaction story is settled, this may well
  // need to be changed.
  MaybeHandle<Object> h_result = GetJSResult(&env, "h", {});
  CHECK(h_result.ToHandleChecked()->IsWasmArray());
#if OBJECT_PRINT
  h_result.ToHandleChecked()->Print();
#endif
}

}  // namespace test_gc
}  // namespace wasm
}  // namespace internal
}  // namespace v8
