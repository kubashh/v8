// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --fuzzing --disable-abortjs --disable-in-process-stack-traces
// --assert-types --maglev --turboshaft --harmony-struct
// --optimize-on-next-call-optimizes-to-maglev --no-lazy-feedback-allocation
// --turbo-stress-instruction-scheduling --fuzzing --turbofan
// --no-always-turbofan --js-explicit-resource-management

function loop() {
  try {
    loop();
  } catch (e) {
  }
  async_function();
}
assertThrows(()=>loop(), RangeError);
async function dispose_function() {
  let stack = new AsyncDisposableStack();
  stack.use();
  await stack.disposeAsync();
}
async function async_function() {
  await dispose_function();
}
