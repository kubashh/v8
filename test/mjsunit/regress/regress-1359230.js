// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --fuzzing


let v0 = -1.7976931348623157e+308;
const v4 = d8.serializer.serialize(v0);
const v5 = new Uint8Array(v4);
v5[2] = 73;
try {
  d8.serializer.deserialize(v4);
} catch(e) { }


const str = /\dei7/sgiuy;
// Length needs to have little endian byte order
// on big endian platforms.
let buffer = new DataView(new ArrayBuffer(8));
buffer.setFloat64(0, 9007199254740991, true);
var h16 = new Int16Array(1);
var h8 = new Int8Array(h16.buffer);
h16[0] = 1;
let l;
if (h8[0] == 0)
    l = buffer.getFloat64(0, false);
else
    l = buffer.getFloat64(0, true);
const obj = {"a":str, "length":l};
const increment = 2061353130;
let n = increment * 21;
for (let i = 0; i < 52; i++) {
  n += increment;
  try {
    const v9 =  d8.serializer.serialize(obj);
    const v10 = new Uint8Array(v9);
    v10[6] = n;
    const v11 =  d8.serializer.deserialize(v9);
  } catch(v12) {
  }
}
