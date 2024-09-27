// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --js-explicit-resource-management

async function loop() {
  function disposal() {}
  const v10 = new AsyncDisposableStack();
  v10[Symbol.dispose] = disposal;
  await v10.use(v10).disposeAsync();
  return loop();
}
assertThrowsAsync(loop(), RangeError);
