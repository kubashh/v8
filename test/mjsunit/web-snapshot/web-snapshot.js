// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

function call(f) {
    return '(' + f.toString() + ')()';
}

(function TestMinimal() {
    let r1 = Realm.create();
    function initialize() {
        globalThis.foo = {
            'str': 'hello',
            'n': 42,
        };
    }
    Realm.eval(r1, call(initialize));
    snapshot = Realm.takeSnapshot(r1, ['foo']);

    let r2 = Realm.create();
    function use() {
        return globalThis.foo;
    }
    Realm.useSnapshot(r2, snapshot);
    let foo = Realm.eval(r2, call(use));

    assertEquals(foo.str, 'hello');
    assertEquals(foo.n, 42);
})();
