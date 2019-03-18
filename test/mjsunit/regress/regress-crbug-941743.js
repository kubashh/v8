// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --noenable-slow-asserts

// Optimize on a holey smi array.
let a = [1, 2, , , 3, ];
function mapping() {
  return a.map(v => v + 1);
}
mapping();
mapping();
%OptimizeFunctionOnNextCall(mapping);
mapping();
// Get the array to a quite large size, though it remains in FastElements.
// (ie, not a dictionary).
a.length = (32 * 1024 * 1024) - 1;
a.fill(1, 0);
a.push(2);
// We should deopt, on Array.prototype.maps attempt to create an output
// array this big.
mapping();
