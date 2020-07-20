// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --wasm-dynamic-tiering --liftoff --no-wasm-tier-up --no-stress-opt

load('test/mjsunit/wasm/wasm-module-builder.js');

const num_functions = 100;
const num_iterations = 5;

function create_builder(delta = 0) {
  const builder = new WasmModuleBuilder();
  let main_body = []
  for (let i = 0; i < num_functions; ++i) {
    let kFunction = builder.addFunction('f' + i, kSig_i_v)
      .addBody(wasmI32Const(i + delta))
      .exportFunc()
      .index;
    main_body = main_body.concat(kExprCallFunction, kFunction, kExprDrop);
  }
  builder.addFunction('main', kSig_v_v)
    .addBody(main_body)
    .exportAs('main');

  return builder;
}

function checkTieredDown(instance) {
  for (let i = 0; i < num_functions; ++i) {
    assertTrue(%IsLiftoffFunction(instance.exports['f' + i]));
  }
}

function checkTieredUp(instance) {
  // Busy waiting until all functions are tiered up.
  let num_liftoff_functions;
  while (true) {
    num_liftoff_functions = 0;
    for (let i = 0; i < num_functions; ++i) {
      if (%IsLiftoffFunction(instance.exports['f' + i])) {
        num_liftoff_functions++;
      }
    }
    if (num_liftoff_functions == 0) return;
  }
}

function check(instance) {
  checkTieredDown(instance);
  for (let i = 0; i < num_iterations - 1; ++i) {
    instance.exports.main();
  }
  checkTieredDown(instance);
  instance.exports.main();
  checkTieredUp(instance);
}

(function testDynamicTiering() {
  print(arguments.callee.name);
  const instance = create_builder().instantiate();
  check(instance);
})();
