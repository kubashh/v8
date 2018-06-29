// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --harmony-locale

// The following test are not part of the comformance. Just some output in
// English to verify the format does return something reasonable for English.
// It may be changed when we update the CLDR data.
// NOTE: These are UNSPECIFIED behavior in
// http://tc39.github.io/proposal-intl-relative-time/

let shortEnglish = new Intl.RelativeTimeFormat("en", {style: "short", localeMatcher: 'lookup', numeric: 'auto'});

assertEquals('en', shortEnglish.resolvedOptions().locale);

assertEquals('03 sec. ago', shortEnglish.format(-3, 'second'));
assertEquals('02 sec. ago', shortEnglish.format(-2, 'second'));
assertEquals('01 sec. ago', shortEnglish.format(-1, 'second'));
assertEquals('now', shortEnglish.format(0, 'second'));
assertEquals('in 01 sec.', shortEnglish.format(1, 'second'));
assertEquals('in 02 sec.', shortEnglish.format(2, 'second'));

assertEquals('03 min. ago', shortEnglish.format(-3, 'minute'));
assertEquals('02 min. ago', shortEnglish.format(-2, 'minute'));
assertEquals('01 min. ago', shortEnglish.format(-1, 'minute'));
assertEquals('in 00 min.', shortEnglish.format(0, 'minute'));
assertEquals('in 01 min.', shortEnglish.format(1, 'minute'));
assertEquals('in 02 min.', shortEnglish.format(2, 'minute'));

assertEquals('03 hr. ago', shortEnglish.format(-3, 'hour'));
assertEquals('02 hr. ago', shortEnglish.format(-2, 'hour'));
assertEquals('01 hr. ago', shortEnglish.format(-1, 'hour'));
assertEquals('in 00 hr.', shortEnglish.format(0, 'hour'));
assertEquals('in 01 hr.', shortEnglish.format(1, 'hour'));
assertEquals('in 02 hr.', shortEnglish.format(2, 'hour'));

assertEquals('03 days ago', shortEnglish.format(-3, 'day'));
assertEquals('02 days ago', shortEnglish.format(-2, 'day'));
assertEquals('yesterday', shortEnglish.format(-1, 'day'));
assertEquals('today', shortEnglish.format(0, 'day'));
assertEquals('tomorrow', shortEnglish.format(1, 'day'));
assertEquals('in 02 days', shortEnglish.format(2, 'day'));

assertEquals('03 wk. ago', shortEnglish.format(-3, 'week'));
assertEquals('02 wk. ago', shortEnglish.format(-2, 'week'));
assertEquals('last wk.', shortEnglish.format(-1, 'week'));
assertEquals('this wk.', shortEnglish.format(0, 'week'));
assertEquals('next wk.', shortEnglish.format(1, 'week'));
assertEquals('in 02 wk.', shortEnglish.format(2, 'week'));

assertEquals('03 mo. ago', shortEnglish.format(-3, 'month'));
assertEquals('02 mo. ago', shortEnglish.format(-2, 'month'));
assertEquals('last mo.', shortEnglish.format(-1, 'month'));
assertEquals('this mo.', shortEnglish.format(0, 'month'));
assertEquals('next mo.', shortEnglish.format(1, 'month'));
assertEquals('in 02 mo.', shortEnglish.format(2, 'month'));

/* "quarter" is not working in ICU now
assertEquals('03 qtrs. ago', shortEnglish.format(-3, 'quarter'));
assertEquals('02 qtr. ago', shortEnglish.format(-2, 'quarter'));
assertEquals('last qtr.', shortEnglish.format(-1, 'quarter'));
assertEquals('this qtr.', shortEnglish.format(0, 'quarter'));
assertEquals('next qtr.', shortEnglish.format(1, 'quarter'));
assertEquals('in 02 qtrs.', shortEnglish.format(2, 'quarter'));
*/

assertEquals('03 yr. ago', shortEnglish.format(-3, 'year'));
assertEquals('02 yr. ago', shortEnglish.format(-2, 'year'));
assertEquals('last yr.', shortEnglish.format(-1, 'year'));
assertEquals('this yr.', shortEnglish.format(0, 'year'));
assertEquals('next yr.', shortEnglish.format(1, 'year'));
assertEquals('in 02 yr.', shortEnglish.format(2, 'year'));
