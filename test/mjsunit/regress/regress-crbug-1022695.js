// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

(function (foo, PI) {
    'use asm';
    var f = PI.toString;
    function get() {
        f();
    }
    return get;
})(this, new Error())();
