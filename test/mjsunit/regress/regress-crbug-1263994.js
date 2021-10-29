// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

function main() {
  Object.defineProperty(Promise, Symbol.species, {
    value: function (f) {
      f(() => { throw 111}, () => { throw 222});
    }
  });
  const promise = WebAssembly.instantiate(new ArrayBuffer(0x10));
  promise.then();
}
main();
