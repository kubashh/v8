// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbofan

(function() {
  function foo(string) { return string.endsWith('a'); }

  %PrepareFunctionForOptimization(foo);
  assertEquals(false, foo(''));
  assertEquals(true, foo('a'));
  assertEquals(false, foo('ab'));
  assertEquals(true, foo('cba'));
  %OptimizeFunctionOnNextCall(foo);
  assertEquals(false, foo(''));
  assertEquals(true, foo('a'));
  assertEquals(false, foo('ab'));
  assertEquals(true, foo('cba'));
  assertOptimized(foo);
})();

(function() {
  function foo(string) { return string.endsWith('aaaaa'); }

  %PrepareFunctionForOptimization(foo);
  assertEquals(false, foo(''));
  assertEquals(true, foo('aaabaaaaa'));
  assertEquals(false, foo('ababababa'));
  assertEquals(true, foo('cbaaaaaa'));
  %OptimizeFunctionOnNextCall(foo);
  assertEquals(false, foo(''));
  assertEquals(true, foo('aaabaaaaa'));
  assertEquals(false, foo('ababababa'));
  assertEquals(true, foo('cbaaaaaa'));
  assertOptimized(foo);
})();

(function() {
  function f() { return "abc".endsWith(); }

  %PrepareFunctionForOptimization(f);
  assertEquals(false, f());
  assertEquals(false, f());
  %OptimizeFunctionOnNextCall(f);
  assertEquals(false, f());
  assertOptimized(f);
})();

(function() {
  function g(n) { return "cba".endsWith("a", n); }
  %PrepareFunctionForOptimization(g);
  assertEquals(false, g(-1));
  assertEquals(false, g(0));
  assertEquals(false, g(1));
  assertEquals(false, g(2));
  assertEquals(true, g(3));
  assertEquals(true, g(4));
  %OptimizeFunctionOnNextCall(g);
  assertEquals(false, g(-1));
  assertEquals(false, g(0));
  assertEquals(false, g(1));
  assertEquals(false, g(2));
  assertEquals(true, g(3));
  assertEquals(true, g(4));
  assertOptimized(g);
})();

(function() {
  function g(n) { return "cbaaaaaaa".endsWith("aaaa", n); }
  %PrepareFunctionForOptimization(g);
  assertEquals(false, g(-1));
  assertEquals(false, g(0));
  assertEquals(false, g(1));
  assertEquals(false, g(2));
  assertEquals(false, g(3));
  assertEquals(false, g(4));
  assertEquals(false, g(5));
  assertEquals(true, g(6));
  assertEquals(true, g(7));
  assertEquals(true, g(8));
  assertEquals(true, g(9));
  %OptimizeFunctionOnNextCall(g);
  assertEquals(false, g(-1));
  assertEquals(false, g(0));
  assertEquals(false, g(1));
  assertEquals(false, g(2));
  assertEquals(false, g(3));
  assertEquals(false, g(4));
  assertEquals(false, g(5));
  assertEquals(true, g(6));
  assertEquals(true, g(7));
  assertEquals(true, g(8));
  assertEquals(true, g(9));
  assertOptimized(g);
})();

(function() {
  function f(n) { return "cba".endsWith("a", n); }
  %PrepareFunctionForOptimization(f);
  assertEquals(true, f());
  assertEquals(true, f());
  %OptimizeFunctionOnNextCall(f);
  assertEquals(true, f(1073741824));
  assertEquals(true, f(1073741824));
  assertEquals(true, f(1073741824));
  assertOptimized(f);
})();

(function() {
  function f(str) {
    return str.endsWith('');
  }

  %PrepareFunctionForOptimization(f);
  f('foo');
  f('');
  %OptimizeFunctionOnNextCall(f);
  assertEquals(f('foo'), true);
  assertEquals(f(''), true);
  assertOptimized(f);
})();
