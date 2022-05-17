// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

const kLoopIterCount = 1000000

function bench(length) {
  let arr = [];
  for (let i = 0; i < length; i++) {
    arr.push(i);
  }

  function testIndexOf(idx) {
    return arr.indexOf(idx);
  }

  // Indices inside the array
  let timer = performance.now();
  for (let i = 0; i < kLoopIterCount; i++) {
    arr.indexOf(i % length);
  }
  timer = performance.now() - timer;

  console.log("[inside] size =", length, "-->", timer);

  timer = performance.now();
  for (let i = 0; i < kLoopIterCount; i++) {
    arr.indexOf(length+i);
  }
  timer = performance.now() - timer;

  console.log("[outside] size =", length, "-->", timer);
}


let sizes = [ 1, 2, 3, 4, 5, 8, 10, 12, 14, 16, 18, 20, 24, 28, 32, 40, 48, 56, 64, 72, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384, 448, 512, 640, 768, 896, 1024 ]

for (const size of sizes) {
  bench(size);
}
