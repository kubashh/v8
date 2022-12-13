// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbofan --no-always-turbofan

const bi = 18446744073709551615n; // 2n ** 64n - 1n

function storeAndLoad(x) {
  let buffer = new ArrayBuffer(80);
  let biArray = new BigInt64Array(buffer);
  biArray[5] = bi;
  biArray[9] = x;
  return biArray[5] + biArray[9];
}

%PrepareFunctionForOptimization(storeAndLoad);
assertEquals(-1n, storeAndLoad(0n));
%OptimizeFunctionOnNextCall(storeAndLoad);
assertEquals(41n, storeAndLoad(2n ** 64n + 42n));
assertEquals(0n, storeAndLoad(-bi));
assertOptimized(storeAndLoad);
assertEquals(-1n, storeAndLoad(false));
assertUnoptimized(storeAndLoad);

%PrepareFunctionForOptimization(storeAndLoad);
assertEquals(-1n, storeAndLoad(0n));
%OptimizeFunctionOnNextCall(storeAndLoad);
assertEquals(0n, storeAndLoad(true));
// TODO(panq): Uncomment the assertion once the deopt loop is eliminated.
// assertOptimized(storeAndLoad);

function storeAndLoadUnsigned(x) {
  let buffer = new ArrayBuffer(80);
  let biArray = new BigUint64Array(buffer);
  biArray[5] = bi;
  biArray[9] = x;
  return biArray[5] + biArray[9];
}

%PrepareFunctionForOptimization(storeAndLoadUnsigned);
assertEquals(bi, storeAndLoadUnsigned(0n));
%OptimizeFunctionOnNextCall(storeAndLoadUnsigned);
assertEquals(bi + 42n, storeAndLoadUnsigned(2n ** 64n + 42n));
assertOptimized(storeAndLoadUnsigned);
assertEquals(bi, storeAndLoadUnsigned(false));
assertUnoptimized(storeAndLoadUnsigned);

%PrepareFunctionForOptimization(storeAndLoadUnsigned);
assertEquals(bi, storeAndLoadUnsigned(0n));
%OptimizeFunctionOnNextCall(storeAndLoadUnsigned);
assertEquals(bi + 1n, storeAndLoadUnsigned(true));
// TODO(panq): Uncomment the assertion once the deopt loop is eliminated.
// assertOptimized(storeAndLoadUnsigned);
