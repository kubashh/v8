// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --opt --no-always-opt

function f(x) {
  %TurbofanStaticAssert(x.foo === 42);
}

function main(b) {
  const x = new Object();
  const y = x;
  if (b) return;

  x.foo = 42;
  f(y);
}


%PrepareFunctionForOptimization(f);
%PrepareFunctionForOptimization(main);

f({a: 1});
f({b: 1});
f({c: 1});
f({d: 1});

main(true);
main(true);
main(false);
main(false);
%OptimizeFunctionOnNextCall(main);
main(Math.pow(2, 10) > 1000);
