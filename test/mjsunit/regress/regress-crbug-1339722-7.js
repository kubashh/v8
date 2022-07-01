// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --noop-on-failed-access-check

d8.file.execute('test/mjsunit/regress/regress-crbug-1321899.js');

const realm = Realm.createAllowCrossRealmAccess();
const globalProxy = Realm.global(realm);

checkHasAccess(globalProxy);

Realm.navigate(realm);

new B(globalProxy);
new C(globalProxy);
new D(globalProxy);
new E(globalProxy);
B.setField(globalProxy);
// The failed access check callback is supposed to throw.
// If it doesn't the behavior is quite quirky.
// This only documents the current behavior.
assertEquals(undefined, B.getField(globalProxy));
assertFalse(B.hasField(globalProxy));
C.setField(globalProxy);
assertEquals(undefined, C.getField(globalProxy));
assertFalse(C.hasField(globalProxy));
D.setAccessor(globalProxy)
assertEquals("d", D.getAccessor(globalProxy));
assertFalse(D.hasAccessor(globalProxy));
assertThrows(() => E.setMethod(globalProxy), TypeError, /Private method '#e' is not writable/);
assertEquals(0, E.getMethod(globalProxy)());
assertFalse(E.hasMethod(globalProxy));
