// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

"use strict";


function RandomHexDigit(allow_zero) {
  const chars = allow_zero ? '0123456789ABCDEF' : '123456789ABCDEF';
  return chars.charAt(Math.floor(Math.random() * chars.length));
}


// Some benchmarks shall compute sums but the result must not grow in terms
// of digits. These can use "small" BigInts, which are BigInts where the most
// significant digit is very small, so it does not overflow.
function SmallRandomBigIntWithBits(bits) {
  console.assert(bits % 8 === 0);
  let bytes = bits / 8;

  if (bytes <= 0) {
    return 0n;
  }

  let s = "0x" + RandomHexDigit(false);
  // Digits are at least 4 bytes long, so we round down to the next smaller
  // multiple of 4 to keep the most significant digit small.
  bytes = Math.floor((bytes - 1) / 4) * 4;
  for(; bytes > 0; --bytes) {
    s += RandomHexDigit(true);
    s += RandomHexDigit(true);
  }

  return BigInt(s);
}


function MaxBigIntWithBits(bits) {
  console.assert(bits % 8 === 0);
  let bytes = bits / 8;

  if (bytes <= 0) {
    return 0n;
  }

  let s = "0x";
  for(; bytes > 0; --bytes) {
    s += "FF";
  }

  return BigInt(s);
}
