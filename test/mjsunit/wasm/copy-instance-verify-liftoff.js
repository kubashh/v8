// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --liftoff

// This test makes sure that the is_liftoff property is
// correctly set for functions after instantiating a new instance.

load('test/mjsunit/wasm/wasm-constants.js');
load('test/mjsunit/wasm/wasm-module-builder.js');

function testLiftoffSync() {
  const builder = new WasmModuleBuilder();
  builder.addFunction('i32_add', kSig_i_ii)
      .addBody([kExprGetLocal, 0, kExprGetLocal, 1, kExprI32Add])
      .exportFunc();

  const module = new WebAssembly.Module(builder.toBuffer());
  const instance = new WebAssembly.Instance(module);
  const instance2 = new WebAssembly.Instance(module);

  assertTrue(%IsLiftoffFunction(instance.exports.i32_add));
  assertTrue(%IsLiftoffFunction(instance2.exports.i32_add));
};

testLiftoffSync()
