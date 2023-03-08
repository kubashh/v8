// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --fuzzing --disable-abortjs --disable-in-process-stack-traces --always-turbofan

for (let v0 = 0; v0 < 100; v0++) {
    for (const v1 in v0) {
        v0[v1] ||= v0;
        function f2(a3, a4) {
            return a3;
        }
    }
    break;
}
