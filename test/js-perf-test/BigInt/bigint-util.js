// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

"use strict";

// for 64 bit machines
const is64bit = true;

function Rng(seed) {
  if (!seed) {
    seed = 138502343;
  }
  this._seed = seed % 2147483647;
  if (this._seed <= 0) {
    this._seed += 2147483646;
  }
}

Rng.prototype.random = function() {
  return this._seed = this._seed * 16807 % 2147483647;
};


const default_rng = new Rng(null);

function MaxBigIntWithDigits(digits, neg) {

  if (digits <= 0)
    return 0n;

  let s = "0x";
  for (let i = 0; i < (digits * (is64bit ? 8 : 4)); ++i) {
    s += "FF";
  }

  return neg ? -BigInt(s) : BigInt(s);
}

function RandomHexDigit(noZero, rng) {
  if (rng == null) {
    rng = default_rng;
  }
  const chars = noZero ? '123456789ABCDEF' : '0123456789ABCDEF';
    return chars.charAt(rng.random() % chars.length);
}

function RandomBigIntWithDigits(digits, neg, rng) {
  if (digits <= 0)
    return 0n;

  let s = "0x" + RandomHexDigit(true, rng);
  for (let i = 0; i < (digits * (is64bit ? 16 : 8) - 1); ++i) {
    s += RandomHexDigit(false, rng);
  }

  return neg ? -BigInt(s) : BigInt(s);
}

function SmallRandomBigIntWithDigits(digits, neg, rng) {
  if (digits <= 0)
    return 0n;

  let s = "0x" + RandomHexDigit(true, rng) + RandomHexDigit(false, rng);
  for (let i = 0; i < ((digits - 1) * (is64bit ? 16 : 8)); ++i) {
    s += RandomHexDigit(false, rng);
  }

  return neg ? -BigInt(s) : BigInt(s);
}
