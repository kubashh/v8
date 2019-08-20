// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

function __f_5() {
  x ** -9 === '';
};
%OptimizeFunctionOnNextCall(__f_5);
__f_5();
