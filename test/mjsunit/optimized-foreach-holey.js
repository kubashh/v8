// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --expose-gc --turbo-inline-array-builtins

(function() {
  var result = 0;
  var f = function() {
    var b = [,,];
    b[0] = 0;
    b[2] = 2;
    var sum = function(v,i,o) {
      result += i;
    };
    b.forEach(sum);
  }
  f();
  f();
  %OptimizeFunctionOnNextCall(f);
  f();
  f();
  f();
  assertEquals(10, result);
})();

(function() {
  var proto_set_func = function(p, s) {
    %NeverOptimizeFunction(proto_set_func);
    if (s) {
      p[0] = 1;
    }
  }
  var p = {};
  var result = 0;
  var f = function(proto_set) {
    var b = [,,];
    p.__proto__ = b.__proto__;
    b.__proto__ = p;
    proto_set_func(p, proto_set);
    b[1] = 2;
    b[2] = 3;
    var sum = function(v,i,o) {
      result += v;
    };
    b.forEach(sum);
  }
  f(false);
  f(false);
  %OptimizeFunctionOnNextCall(f);
  f(true);
  f(false);
  f(false);
  assertEquals(28, result);
})();

(function() {
  var proto_set_func = function(p, s) {
    %NeverOptimizeFunction(proto_set_func);
    if (s) {
      p[0] = 1;
    }
  }
  var p = {};
  var result = 0;
  var f = function(proto_set) {
    var b = [,,];
    p.__proto__ = b.__proto__;
    b.__proto__ = p;
    b[1] = 2;
    b[2] = 3;
    var sum = function(v,i,o) {
      if (i==1) proto_set_func(p, proto_set);
      result += v;
    };
    b.forEach(sum);
  }
  f(false);
  f(false);
  %OptimizeFunctionOnNextCall(f);
  f(true);
  f(false);
  f(false);
  assertEquals(27, result);
})();
