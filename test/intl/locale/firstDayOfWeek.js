// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

assertThrows(() => new Intl.Locale('en', {firstDayOfWeek: 'abc'}), RangeError);
assertThrows(() => new Intl.Locale('en', {firstDayOfWeek: 'MON'}), RangeError);
assertThrows(() => new Intl.Locale('en', {firstDayOfWeek: 'Monday'}), RangeError);
assertThrows(() => new Intl.Locale('en', {firstDayOfWeek: 'Mon'}), RangeError);

assertEquals(undefined, (new Intl.Locale('en')).firstDayOfWeek);
assertEquals('mon', (new Intl.Locale('en', {firstDayOfWeek: 'mon'})).firstDayOfWeek);
assertEquals('tue', (new Intl.Locale('en', {firstDayOfWeek: 'tue'})).firstDayOfWeek);
assertEquals('wed', (new Intl.Locale('en', {firstDayOfWeek: 'wed'})).firstDayOfWeek);
assertEquals('thu', (new Intl.Locale('en', {firstDayOfWeek: 'thu'})).firstDayOfWeek);
assertEquals('fri', (new Intl.Locale('en', {firstDayOfWeek: 'fri'})).firstDayOfWeek);
assertEquals('sat', (new Intl.Locale('en', {firstDayOfWeek: 'sat'})).firstDayOfWeek);
assertEquals('sun', (new Intl.Locale('en', {firstDayOfWeek: 'sun'})).firstDayOfWeek);

assertEquals('mon', (new Intl.Locale('en-u-fw-mon')).firstDayOfWeek);
assertEquals('tue', (new Intl.Locale('en-u-fw-tue')).firstDayOfWeek);
assertEquals('wed', (new Intl.Locale('en-u-fw-wed')).firstDayOfWeek);
assertEquals('thu', (new Intl.Locale('en-u-fw-thu')).firstDayOfWeek);
assertEquals('fri', (new Intl.Locale('en-u-fw-fri')).firstDayOfWeek);
assertEquals('sat', (new Intl.Locale('en-u-fw-sat')).firstDayOfWeek);
assertEquals('sun', (new Intl.Locale('en-u-fw-sun')).firstDayOfWeek);

// Different casing
assertEquals('mon', (new Intl.Locale('en-u-fw-Mon')).firstDayOfWeek);
assertEquals('tue', (new Intl.Locale('en-u-fw-tUe')).firstDayOfWeek);
assertEquals('wed', (new Intl.Locale('en-u-Fw-weD')).firstDayOfWeek);
assertEquals('thu', (new Intl.Locale('en-u-fW-THu')).firstDayOfWeek);
assertEquals('fri', (new Intl.Locale('en-u-FW-fRI')).firstDayOfWeek);
assertEquals('sat', (new Intl.Locale('en-U-fw-SaT')).firstDayOfWeek);
assertEquals('sun', (new Intl.Locale('en-u-fw-SUN')).firstDayOfWeek);

// With differnt calendar
assertEquals('mon', (new Intl.Locale('en-u-fw-mon-ca-roc')).firstDayOfWeek);
assertEquals('tue', (new Intl.Locale('en-u-fw-tue-ca-islamic')).firstDayOfWeek);
assertEquals('wed', (new Intl.Locale('en-u-fw-wed-ca-japanese')).firstDayOfWeek);
assertEquals('thu', (new Intl.Locale('en-u-fw-thu-ca-persian')).firstDayOfWeek);
assertEquals('fri', (new Intl.Locale('en-u-fw-fri-ca-gregory')).firstDayOfWeek);
assertEquals('sat', (new Intl.Locale('en-u-fw-sat-ca-indian')).firstDayOfWeek);
assertEquals('sun', (new Intl.Locale('en-u-fw-sun-ca-coptic')).firstDayOfWeek);
assertEquals('wed', (new Intl.Locale('en-u-fw-wed-ca-iso8601')).firstDayOfWeek);
