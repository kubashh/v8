// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/bigint/div-helpers.h"

#include "src/bigint/bigint-internal.h"

namespace v8 {
namespace bigint {

// Z := X << shift
// Z and X may alias for an in-place shift.
void LeftShift(RWDigits Z, Digits X, int shift) {
  DCHECK(shift >= 0);  // NOLINT(readability/check)
  DCHECK(shift < kDigitBits);
  DCHECK(Z.len() >= X.len());
  if (shift == 0) {
    if (Z == X) return;
    int i = 0;
    for (; i < X.len(); i++) Z[i] = X[i];
    for (; i < Z.len(); i++) Z[i] = 0;
    return;
  }
  digit_t carry = 0;
  int i = 0;
  for (; i < X.len(); i++) {
    digit_t d = X[i];
    Z[i] = (d << shift) | carry;
    carry = d >> (kDigitBits - shift);
  }
  for (; i < Z.len(); i++) {
    Z[i] = carry;
    carry = 0;
  }
  DCHECK(carry == 0);  // NOLINT(readability/check)
}

// Z := X >> shift
// Z and X may alias for an in-place shift.
void RightShift(RWDigits Z, Digits X, int shift) {
  DCHECK(shift >= 0);  // NOLINT(readability/check)
  DCHECK(shift < kDigitBits);
  X.Normalize();
  DCHECK(Z.len() >= X.len());
  if (shift == 0) {
    if (Z == X) return;
    int i = 0;
    for (; i < X.len(); i++) Z[i] = X[i];
    for (; i < Z.len(); i++) Z[i] = 0;
    return;
  }
  digit_t carry = X[0] >> shift;
  int last = X.len() - 1;
  for (int i = 0; i < last; i++) {
    digit_t d = X[i + 1];
    Z[i] = (d << (kDigitBits - shift)) | carry;
    carry = d >> shift;
  }
  Z[last] = carry;
  for (int i = last + 1; i < Z.len(); i++) Z[i] = 0;
}

}  // namespace bigint
}  // namespace v8
