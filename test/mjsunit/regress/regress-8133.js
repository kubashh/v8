// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

const arr = [1, , 3];

function mapper(x) {
  Array.prototype[1] = 2;
  return x + 1;
}

// This iterates over arr using the iterator protocol, which turns the hole into
// undefined.  The mapper function then gets called in a separate iteration over
// the acquired elements, where it increments undefined, which produces NaN and
// gets converted to 0.
assertArrayEquals([2, 0, 4], Uint16Array.from(arr, mapper));

// Now we force a direct iteration (using the array length, not the iterator
// protocol). The mapper function gets called during this iteration, not in a
// separate one. Hence when index 1 is read, 2 is retrieved from the prototype
// and incremented to 3.
Array.prototype[Symbol.iterator] = undefined;
assertArrayEquals([2, 3, 4], Uint16Array.from(arr, mapper));
