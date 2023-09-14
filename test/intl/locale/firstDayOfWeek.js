// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

assertThrows(() => new Intl.Locale('en', {firstDayOfWeek: 'abc'}), RangeError);
assertThrows(() => new Intl.Locale('en', {firstDayOfWeek: 'MON'}), RangeError);
assertThrows(() => new Intl.Locale('en', {firstDayOfWeek: 'Monday'}), RangeError);
assertThrows(() => new Intl.Locale('en', {firstDayOfWeek: 'Mon'}), RangeError);

assertEquals(undefined, (new Intl.Locale('en')).firstDayOfWeek);
assertEquals(1, (new Intl.Locale('en', {firstDayOfWeek: 'mon'})).firstDayOfWeek);
assertEquals(2, (new Intl.Locale('en', {firstDayOfWeek: 'tue'})).firstDayOfWeek);
assertEquals(3, (new Intl.Locale('en', {firstDayOfWeek: 'wed'})).firstDayOfWeek);
assertEquals(4, (new Intl.Locale('en', {firstDayOfWeek: 'thu'})).firstDayOfWeek);
assertEquals(5, (new Intl.Locale('en', {firstDayOfWeek: 'fri'})).firstDayOfWeek);
assertEquals(6, (new Intl.Locale('en', {firstDayOfWeek: 'sat'})).firstDayOfWeek);
assertEquals(7, (new Intl.Locale('en', {firstDayOfWeek: 'sun'})).firstDayOfWeek);

assertEquals(1, (new Intl.Locale('en', {firstDayOfWeek: '1'})).firstDayOfWeek);
assertEquals(2, (new Intl.Locale('en', {firstDayOfWeek: '2'})).firstDayOfWeek);
assertEquals(3, (new Intl.Locale('en', {firstDayOfWeek: '3'})).firstDayOfWeek);
assertEquals(4, (new Intl.Locale('en', {firstDayOfWeek: '4'})).firstDayOfWeek);
assertEquals(5, (new Intl.Locale('en', {firstDayOfWeek: '5'})).firstDayOfWeek);
assertEquals(6, (new Intl.Locale('en', {firstDayOfWeek: '6'})).firstDayOfWeek);
assertEquals(7, (new Intl.Locale('en', {firstDayOfWeek: '7'})).firstDayOfWeek);
assertEquals(7, (new Intl.Locale('en', {firstDayOfWeek: '0'})).firstDayOfWeek);
assertEquals(1, (new Intl.Locale('en', {firstDayOfWeek: 1})).firstDayOfWeek);
assertEquals(2, (new Intl.Locale('en', {firstDayOfWeek: 2})).firstDayOfWeek);
assertEquals(3, (new Intl.Locale('en', {firstDayOfWeek: 3})).firstDayOfWeek);
assertEquals(4, (new Intl.Locale('en', {firstDayOfWeek: 4})).firstDayOfWeek);
assertEquals(5, (new Intl.Locale('en', {firstDayOfWeek: 5})).firstDayOfWeek);
assertEquals(6, (new Intl.Locale('en', {firstDayOfWeek: 6})).firstDayOfWeek);
assertEquals(7, (new Intl.Locale('en', {firstDayOfWeek: 7})).firstDayOfWeek);
assertEquals(7, (new Intl.Locale('en', {firstDayOfWeek: 0})).firstDayOfWeek);

assertEquals(1, (new Intl.Locale('en-u-fw-mon')).firstDayOfWeek);
assertEquals(2, (new Intl.Locale('en-u-fw-tue')).firstDayOfWeek);
assertEquals(3, (new Intl.Locale('en-u-fw-wed')).firstDayOfWeek);
assertEquals(4, (new Intl.Locale('en-u-fw-thu')).firstDayOfWeek);
assertEquals(5, (new Intl.Locale('en-u-fw-fri')).firstDayOfWeek);
assertEquals(6, (new Intl.Locale('en-u-fw-sat')).firstDayOfWeek);
assertEquals(7, (new Intl.Locale('en-u-fw-sun')).firstDayOfWeek);

// Different casing
assertEquals(1, (new Intl.Locale('en-u-fw-Mon')).firstDayOfWeek);
assertEquals(2, (new Intl.Locale('en-u-fw-tUe')).firstDayOfWeek);
assertEquals(3, (new Intl.Locale('en-u-Fw-weD')).firstDayOfWeek);
assertEquals(4, (new Intl.Locale('en-u-fW-THu')).firstDayOfWeek);
assertEquals(5, (new Intl.Locale('en-u-FW-fRI')).firstDayOfWeek);
assertEquals(6, (new Intl.Locale('en-U-fw-SaT')).firstDayOfWeek);
assertEquals(7, (new Intl.Locale('en-u-fw-SUN')).firstDayOfWeek);

// With differnt calendar
assertEquals(1, (new Intl.Locale('en-u-fw-mon-ca-roc')).firstDayOfWeek);
assertEquals(2, (new Intl.Locale('en-u-fw-tue-ca-islamic')).firstDayOfWeek);
assertEquals(3, (new Intl.Locale('en-u-fw-wed-ca-japanese')).firstDayOfWeek);
assertEquals(4, (new Intl.Locale('en-u-fw-thu-ca-persian')).firstDayOfWeek);
assertEquals(5, (new Intl.Locale('en-u-fw-fri-ca-gregory')).firstDayOfWeek);
assertEquals(6, (new Intl.Locale('en-u-fw-sat-ca-indian')).firstDayOfWeek);
assertEquals(7, (new Intl.Locale('en-u-fw-sun-ca-coptic')).firstDayOfWeek);
assertEquals(3, (new Intl.Locale('en-u-fw-wed-ca-iso8601')).firstDayOfWeek);
