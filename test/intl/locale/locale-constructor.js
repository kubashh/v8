// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Locale constructor can't be called as function.
assertThrows(() => Intl.Locale('sr'), TypeError);

// Non-string locale.
assertThrows(() => new Intl.Locale(5), TypeError);

// Invalid locale.
assertThrows(() => new Intl.Locale('abcdefghi'), TypeError);

// Options has to be an Object.
assertThrows(() => new Intl.Locale('sr', 5), TypeError);

// ICU problem - locale length is limited.
// http://bugs.icu-project.org/trac/ticket/13417.
assertThrows(
    () => new Intl.Locale('sr-cyrl-rs-t-ja-u-ca-islamic-x-whatever', {
      calendar: 'buddhist',
      caseFirst: 'true',
      collation: 'phonebk',
      currency: 'RSD',
      hourCycle: 'h23',
      caseFirst: 'upper',
      numeric: 'true',
      numberingSystem: 'roman',
      timeZone: 'uslax'
    }),
    TypeError);
