// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Ignore the first tag when checking for duplicate subtags.
assertDoesNotThrow(() => Intl.getCanonicalLocales("foobar-foobar"));

// Ignore duplicate subtags in different namespaces; eg, 'a' vs 'u'.
assertDoesNotThrow(() => Intl.getCanonicalLocales("en-a-ca-Chinese-u-ca-Chinese"));

// Check duplicate subtags (after the first tag) are detected.
assertThrows(() => Intl.getCanonicalLocales("en-foobar-foobar"), RangeError);

// Duplicate subtags are valid as per the ECMA262 spec.
//
// TODO(gsathya): Uncomment once ICU dep is rolled in:
// https://chromium-review.googlesource.com/c/v8/v8/+/1239190/1
//assertDoesNotThrow(() => Intl.getCanonicalLocales("en-u-ca-gregory-ca-chinese"));
