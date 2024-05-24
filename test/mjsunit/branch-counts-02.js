// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --maglev-branch-feedback
// Flags: --allow-natives-syntax --maglev --sparkplug

function block1() {}
function block2() {}

function foo(a) {
  // JumpIfTrue; we jump ahead if "a == 0x222" is true.
  if (a != 0x222) {
    block1();
  }
  block2();
}
%PrepareFunctionForOptimization(foo);
%CompileBaseline(foo);

foo(0x222);
foo(1);
foo(0x222);
foo(0x222);
foo(0x222);

const counts = %GetBranchCounts(foo);
assertEquals(4, counts[0]); // Did jump.
assertEquals(1, counts[1]); // Did not jump.
