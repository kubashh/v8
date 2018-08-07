// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --expose-wasm

load('test/mjsunit/wasm/wasm-constants.js');
load('test/mjsunit/wasm/wasm-module-builder.js');

function CreateDefaultBuilder() {
  const builder = new WasmModuleBuilder();

  const sig_index = kSig_i_dd;
  builder.addImport('mod', 'fun', sig_index);
  builder.addFunction('main', sig_index)
      .addBody([
        kExprGetLocal, 0,      // --
        kExprGetLocal, 1,      // --
        kExprCallFunction, 0,  // --
      ])                       // --
      .exportFunc();
  return builder;
}

function instantiateWithFFI(ffi) {
  return CreateDefaultBuilder().instantiate(ffi);
}

function asyncInstantiateWithFFI(ffi) {
  return CreateDefaultBuilder().asyncInstantiate(ffi);
}

// everything is good.
(function valid_ffi() {
  print(arguments.callee.name);
  let ffi = {
    'mod': {
      fun: function(a, b) {
        print(a, b);
      }
    }
  };
  instantiateWithFFI(ffi);
  assertPromiseResult(
      asyncInstantiateWithFFI(ffi), undefined, assertUnreachable);
})();

function testForError(ffi, error) {
  assertThrows(function() {
    instantiateWithFFI(ffi);
  }, error);

  assertPromiseResult(
      asyncInstantiateWithFFI(ffi), assertUnreachable,
      e => assertInstanceof(e, error));
}

// FFI object should be an object.
testForError(17, TypeError);
testForError({}, TypeError);
testForError({mod: {}}, WebAssembly.LinkError);
testForError({mod: {fun: {}}}, WebAssembly.LinkError);
testForError({mod: {fun: 0}}, WebAssembly.LinkError);

(function testImportWithInvalidSignature() {
  print(arguments.callee.name);
  // "fun" should have signature "i_dd"
  let builder = new WasmModuleBuilder();

  let sig_index = kSig_i_dd;
  builder.addFunction('exp', kSig_i_i)
      .addBody([
        kExprGetLocal,
        0,
      ])  // --
      .exportFunc();

  let exported = builder.instantiate().exports.exp;
  testForError({mod: {fun: exported}}, WebAssembly.LinkError);
})();

(function regression870646() {
  print(arguments.callee.name);
  const ffi = {mod: {fun: function() {}}};
  Object.defineProperty(ffi, 'mod', {
    get: function() {
      throw 'my_exception';
    }
  });

  assertThrows(_ => instantiateWithFFI(ffi));

  assertPromiseResult(
      asyncInstantiateWithFFI(ffi), assertUnreachable,
      e => assertEquals(e, 'my_exception'));
})();

// "fun" matches signature "i_dd"
(function testImportWithValidSignature() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  builder.addFunction('exp', kSig_i_dd)
      .addBody([
        kExprI32Const,
        33,
      ])  // --
      .exportFunc();

  let exported = builder.instantiate().exports.exp;
  let instance = instantiateWithFFI({mod: {fun: exported}});
  assertEquals(33, instance.exports.main());
  // Test also the async variant.
  assertPromiseResult(
      asyncInstantiateWithFFI({mod: {fun: exported}}), instance => {
        assertEquals(33, instance.exports.main());
      });
})();

(function I64InSignatureThrows() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  builder.addMemory(1, 1, true);
  builder.addFunction('function_with_invalid_signature', kSig_l_ll)
    .addBody([           // --
      kExprGetLocal, 0,  // --
      kExprGetLocal, 1,  // --
      kExprI64Sub])      // --
    .exportFunc()

  let module = builder.instantiate();

  assertThrows(function() {
    module.exports.function_with_invalid_signature(33, 88);
  }, TypeError);

  // Test also the async variant.
  assertPromiseResult(
      builder.asyncInstantiate(),
      instance => assertThrows(
          instance.exports.function_with_invalid_signature, TypeError));
})();

(function I64ParamsInSignatureThrows() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();

  builder.addMemory(1, 1, true);
  builder.addFunction('function_with_invalid_signature', kSig_i_l)
      .addBody([kExprGetLocal, 0, kExprI32ConvertI64])
      .exportFunc()

          let module = builder.instantiate();

  assertThrows(function() {
    module.exports.function_with_invalid_signature(33);
  }, TypeError);

  // Test also the async variant.
  assertPromiseResult(
      builder.asyncInstantiate(),
      instance => assertThrows(
          instance.exports.function_with_invalid_signature, TypeError));
})();

(function I64JSImportThrows() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let sig_index = builder.addType(kSig_i_i);
  let sig_i64_index = builder.addType(kSig_i_l);
  let index = builder.addImport('', 'func', sig_i64_index);
  builder.addFunction('main', sig_index)
      .addBody([
        kExprGetLocal, 0, kExprI64SConvertI32, kExprCallFunction, index  // --
      ])                                                                 // --
      .exportFunc();
  let func = function() {
    return {};
  };
  let main = builder.instantiate({'': {func: func}}).exports.main;
  assertThrows(function() {
    main(13);
  }, TypeError);

  // Test also the async variant.
  assertPromiseResult(
      builder.asyncInstantiate({'': {func: func}}),
      instance => assertThrows(instance.exports.main, TypeError));
})();

(function ImportI64ParamWithF64ReturnThrows() {
  print(arguments.callee.name);
  // This tests that we generate correct code by using the correct return
  // register. See bug 6096.
  let builder = new WasmModuleBuilder();
  builder.addImport('', 'f', makeSig([kWasmI64], [kWasmF64]));
  builder.addFunction('main', kSig_v_v)
      .addBody([kExprI64Const, 0, kExprCallFunction, 0, kExprDrop])
      .exportFunc();
  let instance = builder.instantiate({'': {f: i => i}});

  assertThrows(instance.exports.main, TypeError);

  // Test also the async variant.
  assertPromiseResult(
      builder.asyncInstantiate({'': {f: i => i}}),
      instance => assertThrows(instance.exports.main, TypeError));
})();

(function ImportI64Return() {
  print(arguments.callee.name);
  // This tests that we generate correct code by using the correct return
  // register(s). See bug 6104.
  let builder = new WasmModuleBuilder();
  builder.addImport('', 'f', makeSig([], [kWasmI64]));
  builder.addFunction('main', kSig_v_v)
      .addBody([kExprCallFunction, 0, kExprDrop])
      .exportFunc();
  let instance = builder.instantiate({'': {f: () => 1}});

  assertThrows(instance.exports.main, TypeError);

  // Test also the async variant.
  assertPromiseResult(
      builder.asyncInstantiate({'': {f: () => 1}}),
      instance => assertThrows(instance.exports.main, TypeError));
})();

(function ImportSymbolToNumberThrows() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let index = builder.addImport('', 'func', kSig_i_v);
  builder.addFunction('main', kSig_i_v)
      .addBody([kExprCallFunction, 0])
      .exportFunc();
  let func = () => Symbol();
  let main = builder.instantiate({'': {func: func}}).exports.main;
  assertThrows(main, TypeError);

  // Test also the async variant.
  assertPromiseResult(
      builder.asyncInstantiate({'': {func: func}}),
      instance => assertThrows(instance.exports.main, TypeError));
})();
