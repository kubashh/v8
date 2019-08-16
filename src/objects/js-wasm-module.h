// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_JS_WASM_MODULE_H_
#define V8_OBJECTS_JS_WASM_MODULE_H_

#include "src/objects/module.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

class WasmModuleObject;

// The runtime representation of a Synthetic Module Record, a module that can be
// instantiated by an embedder with embedder-defined exports and evaluation
// steps.
// https://heycam.github.io/webidl/#synthetic-module-records
class JSWasmModule : public Module {
 public:
  NEVER_READ_ONLY_SPACE
  DECL_CAST(JSWasmModule)
  DECL_VERIFIER(JSWasmModule)
  DECL_PRINTER(JSWasmModule)

  // backing Wasm module
  DECL_ACCESSORS(module, WasmModuleObject)

  // Modules imported or re-exported by this module.
  DECL_ACCESSORS(requested_modules, FixedArray)

  // Layout description.
  DEFINE_FIELD_OFFSET_CONSTANTS(Module::kHeaderSize,
                                TORQUE_GENERATED_JSWASM_MODULE_FIELDS)

  using BodyDescriptor =
      SubclassBodyDescriptor<Module::BodyDescriptor,
                             FixedBodyDescriptor<kModuleOffset, kSize, kSize>>;

  static V8_WARN_UNUSED_RESULT Handle<String> GetModuleRequest(
      Isolate* isolate, Handle<JSWasmModule> module, int i);

  static V8_WARN_UNUSED_RESULT Handle<FixedArray> GetExportedNames(
      Isolate* isolate, Handle<JSWasmModule> module);

 private:
  friend class Module;

  static V8_WARN_UNUSED_RESULT MaybeHandle<Cell> ResolveExport(
      Isolate* isolate, Handle<JSWasmModule> module,
      Handle<String> module_specifier, Handle<String> export_name,
      MessageLocation loc, bool must_resolve);
  static V8_WARN_UNUSED_RESULT MaybeHandle<Cell> ResolveImport(
      Isolate* isolate, Handle<SourceTextModule> module, Handle<String> name,
      int module_request, MessageLocation loc, bool must_resolve,
      Module::ResolveSet* resolve_set);

  static V8_WARN_UNUSED_RESULT bool PrepareInstantiate(
      Isolate* isolate, Handle<JSWasmModule> module,
      v8::Local<v8::Context> context, v8::Module::ResolveCallback callback);
  static V8_WARN_UNUSED_RESULT bool FinishInstantiate(
      Isolate* isolate, Handle<JSWasmModule> module);

  static V8_WARN_UNUSED_RESULT MaybeHandle<Object> Evaluate(
      Isolate* isolate, Handle<JSWasmModule> module);

  OBJECT_CONSTRUCTORS(JSWasmModule, Module);
};

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_JS_WASM_MODULE_H_
