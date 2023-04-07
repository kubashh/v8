// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --harmony-struct

const S1 = new SharedStructType(['field']);
const S2 = new SharedStructType(['field2']);

assertTrue(SharedStructType.isSharedStruct(new S1()));
assertTrue(SharedStructType.isSharedStruct(new S2()));

assertTrue(SharedArray.isSharedArray(new SharedArray(1)));

assertTrue(Atomics.Mutex.isMutex(new Atomics.Mutex()));

assertTrue(Atomics.Condition.isCondition(new Atomics.Condition()));

const falsehoods = [
  42, -0, undefined, null, true, false, 'foo', {}, []
];
for (const x of falsehoods) {
  assertFalse(SharedStructType.isSharedStruct(x));
  assertFalse(SharedArray.isSharedArray(x));
  assertFalse(Atomics.Mutex.isMutex(x));
  assertFalse(Atomics.Condition.isCondition(x));
}
