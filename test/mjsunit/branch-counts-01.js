// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --maglev-branch-feedback
// Flags: --allow-natives-syntax --no-always-sparkplug

function block1() {}
function block2() {}

function foo(a) {
  // JumpIfFalse; we jump ahead if "a == 0" is false.
  if (a == 0) {
    block1();
  }
  block2();
}
%PrepareFunctionForOptimization(foo);

foo(0);
foo(1);
foo(0);
foo(1);
foo(1);

const counts = %GetBranchCounts(foo);
assertEquals(3, counts[0]); // Did jump.
assertEquals(2, counts[1]); // Did not jump.
