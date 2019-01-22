// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --harmony-intl-bigint

var locales = [
    "en",  // "1,234,567,890,123,456"
    "de",  // "1.234.567.890.123.456"
    "fr",  // "1 234 567 890 123 456"
    "hi",  // "1,23,45,67,89,01,23,456"
    "fa",  // "۱٬۲۳۴٬۵۶۷٬۸۹۰٬۱۲۳٬۴۵۶"
    "th-u-nu-thai",  // "๑,๒๓๔,๕๖๗,๘๙๐,๑๒๓,๔๕๖"
];

for (var locale of locales) {
  let n = Number.MAX_SAFE_INTEGER;
  let nf = new Intl.NumberFormat(locale);

  let percentOption = {style: "percent"};
  let nfPercent = new Intl.NumberFormat(locale, percentOption);
  // Test NumberFormat w/ number output the same as
  // BigInt.prototype.toLocaleString()
  assertEquals(nf.format(n), BigInt(n).toLocaleString(locale));
  assertEquals(nf.format(-n), BigInt(-n).toLocaleString(locale));
  assertEquals(nf.format(Math.floor(n/2)),
      BigInt(Math.floor(n/2)).toLocaleString(locale));

  // Test NumberFormat output the same regardless pass in as number or BigInt
  assertEquals(nf.format(n), nf.format(BigInt(n)));
  assertEquals(nf.format(-n), nf.format(BigInt(-n)));
  assertEquals(nf.format(Math.floor(n/2)), nf.format(BigInt(Math.floor(n/2))));

  // Test output with option
  // Test NumberFormat w/ number output the same as
  // BigInt.prototype.toLocaleString()
  assertEquals(nfPercent.format(n),
      BigInt(n).toLocaleString(locale, percentOption));
  assertEquals(nfPercent.format(-n),
      BigInt(-n).toLocaleString(locale, percentOption));
  assertEquals(nfPercent.format(Math.floor(n/2)),
      BigInt(Math.floor(n/2)).toLocaleString(locale, percentOption));

  // Test NumberFormat output the same regardless pass in as number or BigInt
  assertEquals(nfPercent.format(n), nfPercent.format(BigInt(n)));
  assertEquals(nfPercent.format(-n), nfPercent.format(BigInt(-n)));
  assertEquals(nfPercent.format(Math.floor(n/2)),
      nfPercent.format(BigInt(Math.floor(n/2))));

  // Test very big BigInt
  let veryBigInt = BigInt(Number.MAX_SAFE_INTEGER) *
      BigInt(Number.MAX_SAFE_INTEGER) *
      BigInt(Number.MAX_SAFE_INTEGER);
  assertEquals(nf.format(veryBigInt), veryBigInt.toLocaleString(locale));
  // It should output different than toString
  assertFalse(veryBigInt.toLocaleString(locale) == veryBigInt.toString());
  assertTrue(veryBigInt.toLocaleString(locale).length > veryBigInt.toString().length);
}
