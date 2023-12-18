// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --concurrent-recompilation-delay=0

function f(x) {
  var y = 0;
  for (var i = 0; i < x; i++) {
    y = 1;
  }
  return y;
}

function g(iter) {
  if (%IsTurbofanEnabled()) {
    while (!%ActiveTierIsTurbofan(f) && --iter) f(10);
  }
}
%NeverOptimizeFunction(g);

if (%SetBatterySaverMode(true)) {
  g(100000);
  assertFalse(%ActiveTierIsTurbofan(f));
  if (%SetBatterySaverMode(false)) {
    g(100000);
    if (%IsTurbofanEnabled()) {
      assertTrue(%ActiveTierIsTurbofan(f));
    }
  }
}
