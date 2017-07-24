// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

function force_deopt() {
  try {
    undefined[{}] = /[abc]/gi;
  } catch(e) {}
}

force_deopt();
force_deopt();
%OptimizeFunctionOnNextCall(force_deopt);
force_deopt();
