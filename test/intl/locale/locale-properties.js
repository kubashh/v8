// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Make sure that locale exposes all required properties. Those not specified
// should have undefined value.

let locale = new Intl.Locale('sr-cyrl-rs-t-ja-u-ca-islamic-x-whatever', {
  calendar: 'buddhist',
  caseFirst: 'true',
  collation: 'phonebk',
  currency: 'RSD',
  hourCycle: 'h23',
  caseFirst: 'upper',
  numeric: 'true',
  numberingSystem: 'roman'
});
assertEquals('sr', locale.language);
assertEquals('Cyrl', locale.script);
assertEquals('RS', locale.region);
assertEquals('sr-Cyrl-RS', locale.baseName);
assertEquals('buddhist', locale.calendar);
assertEquals('phonebk', locale.collation);
assertEquals('rsd', locale.currency);
assertEquals('h23', locale.hourCycle);
assertEquals('upper', locale.caseFirst);
assertEquals(true, locale.numeric);
assertEquals('roman', locale.numberingSystem);
assertEquals(undefined, locale.timeZone);

// Locale with the time zone.
let localeTZ = new Intl.Locale('de-u-tz-uslax');
assertEquals('de', localeTZ.language);
assertEquals('de', localeTZ.baseName);
assertEquals('uslax', localeTZ.timeZone);
