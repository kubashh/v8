// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --js-explicit-resource-management

async function loop() {
  function disposal() {}
  const stack = new AsyncDisposableStack();
  stack[Symbol.dispose] = disposal;
  stack.use(stack).disposeAsync();
  loop();
}
assertThrowsAsync(loop(), RangeError);
