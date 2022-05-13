// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

const sym = %CreatePrivateSymbol('x');
const realm = Realm.create();
const globalProxy = Realm.global(realm);

assertThrows(() => globalProxy[sym] = 1, Error, /no access/);
assertThrows(() => globalProxy[sym], Error, /no access/);

const realm2 = Realm.createAllowCrossRealmAccess();
const crossAccess = Realm.global(realm2);
crossAccess[sym] = 1;
assertEquals(1, crossAccess[sym]);

Realm.detachGlobal(realm2);

assertThrows(() => crossAccess[sym] = 1, Error, /no access/);
assertThrows(() => crossAccess[sym], Error, /no access/);
