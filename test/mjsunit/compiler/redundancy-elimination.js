// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

// Test the RedundancyElimination::ReduceSpeculativeNumberOperation()
// TurboFan optimization for the case of SpeculativeNumberAdd with
// Number feedback.
(function() {
  function bar(i) {
    return ++i;
  }
  bar(0.1);

  function foo(a, i) {
    const x = a[i];
    const y = a[bar(i)];
    return x + y;
  }

  foo([1, 2], 0);
  foo([1, 2], 0);
  %OptimizeFunctionOnNextCall(foo);
  foo([1, 2], 0);
})();

// Test the RedundancyElimination::ReduceSpeculativeNumberOperation()
// TurboFan optimization for the case of SpeculativeNumberAdd with
// NumberOrOddball feedback.
(function() {
  function bar(i) {
    return ++i;
  }
  bar(undefined);

  function foo(a, i) {
    const x = a[i];
    const y = a[bar(i)];
    return x + y;
  }

  foo([1, 2], 0);
  foo([1, 2], 0);
  %OptimizeFunctionOnNextCall(foo);
  foo([1, 2], 0);
})();

// Test the RedundancyElimination::ReduceSpeculativeNumberOperation()
// TurboFan optimization for the case of SpeculativeNumberSubtract with
// Number feedback.
(function() {
  function bar(i) {
    return --i;
  }
  bar(0.1);

  function foo(a, i) {
    const x = a[i];
    const y = a[bar(i)];
    return x + y;
  }

  foo([1, 2], 1);
  foo([1, 2], 1);
  %OptimizeFunctionOnNextCall(foo);
  foo([1, 2], 1);
})();

// Test the RedundancyElimination::ReduceSpeculativeNumberOperation()
// TurboFan optimization for the case of SpeculativeNumberSubtract with
// NumberOrOddball feedback.
(function() {
  function bar(i) {
    return --i;
  }
  bar(undefined);

  function foo(a, i) {
    const x = a[i];
    const y = a[bar(i)];
    return x + y;
  }

  foo([1, 2], 1);
  foo([1, 2], 1);
  %OptimizeFunctionOnNextCall(foo);
  foo([1, 2], 1);
})();

// Test the RedundancyElimination::ReduceSpeculativeNumberOperation()
// TurboFan optimization for the case of SpeculativeToNumber.
(function() {
  function foo(a, i) {
    const x = a[i];
    const y = i++;
    return x + y;
  }

  foo([1, 2], 0);
  foo([1, 2], 0);
  %OptimizeFunctionOnNextCall(foo);
  foo([1, 2], 0);
})();

// Test the RedundancyElimination::ReduceSpeculativeNumberOperation()
// TurboFan optimization for the case of SpeculativeSafeIntegerAdd.
(function() {
  function foo(a, i) {
    const x = a[i];
    const y = a[++i];
    return x + y;
  }

  foo([1, 2], 0);
  foo([1, 2], 0);
  %OptimizeFunctionOnNextCall(foo);
  foo([1, 2], 0);
})();

// Test the RedundancyElimination::ReduceSpeculativeNumberOperation()
// TurboFan optimization for the case of SpeculativeSafeIntegerSubtract.
(function() {
  function foo(a, i) {
    const x = a[i];
    const y = a[--i];
    return x + y;
  }

  foo([1, 2], 1);
  foo([1, 2], 1);
  %OptimizeFunctionOnNextCall(foo);
  foo([1, 2], 1);
})();
