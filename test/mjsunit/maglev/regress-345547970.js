// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --no-testing-d8-test-runner
// Flags: --no-lazy-feedback-allocation --single-threaded

const v0 = [-303801024,-54947];
const v1 = %OptimizeOsr();
for (const v2 in v0) {
    const v5 = String.fromCharCode(55598);
    let v6 = v5.codePointAt();
    const v7 = v6 >>> v6;
    try {
        const v10 = eval(v2);
    } catch(e15) {
    }
}
