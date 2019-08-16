// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/objects/js-wasm-module.h"

#include "src/api/api-inl.h"
#include "src/builtins/accessors.h"
#include "src/objects/js-generator-inl.h"
#include "src/objects/module-inl.h"
#include "src/objects/objects-inl.h"
#include "src/objects/shared-function-info.h"
#include "src/utils/ostreams.h"
#include "src/wasm/module-decoder.h"
#include "src/wasm/module-instantiate.h"

namespace v8 {
namespace internal {

MaybeHandle<Cell> JSWasmModule::ResolveExport(Isolate* isolate,
                                              Handle<JSWasmModule> module,
                                              Handle<String> module_specifier,
                                              Handle<String> export_name,
                                              MessageLocation loc,
                                              bool must_resolve) {
  Handle<Object> object(module->exports().Lookup(export_name), isolate);
  if (object->IsCell()) {
    return Handle<Cell>::cast(object);
  }

  if (must_resolve) {
    return isolate->Throw<Cell>(
        isolate->factory()->NewSyntaxError(MessageTemplate::kUnresolvableExport,
                                           module_specifier, export_name),
        &loc);
  }

  return MaybeHandle<Cell>();
}

// Implements Synthetic Module Record's Instantiate concrete method :
// https://heycam.github.io/webidl/#smr-instantiate
bool JSWasmModule::PrepareInstantiate(Isolate* isolate,
                                      Handle<JSWasmModule> js_module,
                                      v8::Local<v8::Context> context,
                                      v8::Module::ResolveCallback callback) {
  Handle<WasmModuleObject> wasm_module_object(js_module->module(), isolate);

  // set import cells
  {
    Handle<FixedArray> requested_modules(js_module->requested_modules(),
                                         isolate);
    std::vector<wasm::WasmImport> import_table =
        wasm_module_object->module()->import_table;

    for (int i = 0; i < static_cast<int>(import_table.size()); ++i) {
      const wasm::WasmImport& import = import_table[i];

      Handle<String> module_name =
          WasmModuleObject::ExtractUtf8StringFromModuleBytes(
              isolate, wasm_module_object, import.module_name)
              .ToHandleChecked();
      v8::Local<v8::Module> api_requested_module;
      if (!callback(context, v8::Utils::ToLocal(module_name),
                    v8::Utils::ToLocal(Handle<Module>::cast(js_module)))
               .ToLocal(&api_requested_module)) {
        isolate->PromoteScheduledException();
        return false;
      }
      Handle<Module> requested_module =
          Utils::OpenHandle(*api_requested_module);
      requested_modules->set(i, *requested_module);
    }

    // Recurse.
    for (int i = 0, length = requested_modules->length(); i < length; ++i) {
      Handle<Module> requested_module(Module::cast(requested_modules->get(i)),
                                      isolate);
      if (!Module::PrepareInstantiate(isolate, requested_module, context,
                                      callback)) {
        return false;
      }
    }
  }

  // set export cells
  {
    Handle<ObjectHashTable> exports(js_module->exports(), isolate);
    std::vector<wasm::WasmExport> export_table =
        wasm_module_object->module()->export_table;

    for (int i = 0; i < static_cast<int>(export_table.size()); ++i) {
      const wasm::WasmExport& exp = export_table[i];
      Handle<Cell> cell =
          isolate->factory()->NewCell(isolate->factory()->undefined_value());
      Handle<String> name = WasmModuleObject::ExtractUtf8StringFromModuleBytes(
                                isolate, wasm_module_object, exp.name)
                                .ToHandleChecked();
      CHECK(exports->Lookup(name).IsTheHole(isolate));
      exports = ObjectHashTable::Put(exports, name, cell);
    }

    js_module->set_exports(*exports);
  }
  return true;
}

/* // Second step of module instantiation.  No real work to do for
 * SyntheticModule */
/* // as there are no imports or indirect exports to resolve; */
/* // just update status. */
bool JSWasmModule::FinishInstantiate(Isolate* isolate,
                                     Handle<JSWasmModule> js_module) {
  js_module->SetStatus(kInstantiated);
  return true;
}

// https://webassembly.github.io/esm-integration/js-api/index.html#module-execution
MaybeHandle<Object> JSWasmModule::Evaluate(Isolate* isolate,
                                           Handle<JSWasmModule> js_module) {
  js_module->SetStatus(kEvaluating);

  // 2. Let module be record.[[WebAssemblyModule]].
  Handle<WasmModuleObject> module(js_module->module(), isolate);

  // 5. Let importsObject be a new Object.
  MaybeHandle<JSReceiver> importsObject = {};
  CHECK_EQ(js_module->requested_modules().length(), 0);

  // Evaluate the wasm module and fill the export Objects

  wasm::ErrorThrower thrower(isolate, "JSWasmModule::PrepareInstantiate()");

  // 7. Asynchronously instantiate a WebAssembly module module with imports
  // importsObject and let instancePromise be the result.
  // FIXME(ssauleau): not asynchronous?
  Handle<WasmInstanceObject> instance =
      wasm::InstantiateToInstanceObject(isolate, &thrower, module,
                                        importsObject,
                                        Handle<JSArrayBuffer>::null())
          .ToHandleChecked();

  Handle<JSReceiver> instance_exports_object =
      Handle<JSReceiver>(instance->exports_object(), isolate);

  Handle<FixedArray> wasm_exports;
  if (!JSReceiver::OwnPropertyKeys(instance_exports_object)
           .ToHandle(&wasm_exports)) {
    return {};
  }

  Handle<ObjectHashTable> exports(js_module->exports(), isolate);

  for (int index = 0; index < wasm_exports->length(); index++) {
    Handle<String> name(String::cast(wasm_exports->get(index)), isolate);
    Handle<Object> export_object(exports->Lookup(name), isolate);
    CHECK(export_object->IsCell());

    Handle<Cell> export_cell(Handle<Cell>::cast(export_object));
    export_cell->set_value(
        *JSReceiver::GetProperty(isolate, instance_exports_object, name)
             .ToHandleChecked());
  }

  js_module->SetStatus(kEvaluated);
  return isolate->factory()->undefined_value();
}

Handle<String> JSWasmModule::GetModuleRequest(Isolate* isolate,
                                              Handle<JSWasmModule> js_module,
                                              int i) {
  Handle<WasmModuleObject> wasm_module_object(js_module->module(), isolate);
  CHECK_LT(i, js_module->requested_modules().length());

  std::vector<wasm::WasmImport> import_table =
      wasm_module_object->module()->import_table;
  const wasm::WasmImport& import = import_table[i];
  return WasmModuleObject::ExtractUtf8StringFromModuleBytes(
             isolate, wasm_module_object, import.module_name)
      .ToHandleChecked();
}

/* // Return a list of exported names */
/* //
 * https://webassembly.github.io/esm-integration/js-api/index.html#get-exported-names
 */
/* Handle<FixedArray> GetExportedNames(Isolate* isolate, */
/*                                     Handle<JSWasmModule> module) { */
/* } */

}  // namespace internal
}  // namespace v8
