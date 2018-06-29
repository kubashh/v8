// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --harmony-relative-time-format

// RelativeTimeFormat constructor can't be called as function.
assertThrows(() => Intl.RelativeTimeFormat('sr'), TypeError);

// Non-string locale.
//assertThrows(() => new Intl.RelativeTimeFormat(5), TypeError);

// Invalid locale string.
//assertThrows(() => new Intl.RelativeTimeFormat('abcdefghi'), RangeError);

assertDoesNotThrow(() => new Intl.RelativeTimeFormat('sr', {}));

assertDoesNotThrow(() => new Intl.RelativeTimeFormat([], {}));

assertDoesNotThrow(() => new Intl.RelativeTimeFormat(['fr', 'ar'], {}));

assertDoesNotThrow(() => new Intl.RelativeTimeFormat({0: 'ja', 1:'fr'}, {}));

assertDoesNotThrow(() => new Intl.RelativeTimeFormat({1: 'ja', 2:'fr'}, {}));

assertDoesNotThrow(() => new Intl.RelativeTimeFormat('sr'));

assertDoesNotThrow(() => new Intl.RelativeTimeFormat());

assertDoesNotThrow(
    () => new Intl.RelativeTimeFormat(
        'sr', {
          localeMatcher: 'lookup',
          style: 'short',
          numeric: 'always'
        }));


assertDoesNotThrow(
    () => new Intl.RelativeTimeFormat('sr', {localeMatcher: 'lookup'}));

assertDoesNotThrow(
    () => new Intl.RelativeTimeFormat('sr', {localeMatcher: 'best fit'}));

assertThrows(
    () => new Intl.RelativeTimeFormat('sr', {localeMatcher: 'hello'}),
    RangeError);

assertThrows(
    () => new Intl.RelativeTimeFormat('sr', {localeMatcher: 'look up'}),
    RangeError);

assertThrows(
    () => new Intl.RelativeTimeFormat('sr', {localeMatcher: 'bestfit'}),
    RangeError);


assertDoesNotThrow(
    () => new Intl.RelativeTimeFormat('sr', {style: 'long'}));

assertDoesNotThrow(
    () => new Intl.RelativeTimeFormat('sr', {style: 'short'}));

assertDoesNotThrow(
    () => new Intl.RelativeTimeFormat('sr', {style: 'narrow'}));

/*
assertThrows(
    () => new Intl.RelativeTimeFormat('sr', {style: 'giant'}),
    RangeError);
    */

assertDoesNotThrow(
    () => new Intl.RelativeTimeFormat('sr', {numeric: 'always'}));

assertDoesNotThrow(
    () => new Intl.RelativeTimeFormat('sr', {numeric: 'auto'}));

assertThrows(
    () => new Intl.RelativeTimeFormat('sr', {numeric: 'never'}),
    RangeError);

// Throws only once during construction.
// Check for all getters to prevent regression.
// Preserve the order of getter initialization.
let getCount = 0;
let localeMatcher = -1;
let style = -1;
let numeric = -1;

new Intl.RelativeTimeFormat('en-US', {
  get localeMatcher() {
    localeMatcher = ++getCount;
  },
  get style() {
    style = ++getCount;
  },
  get numeric() {
    numeric = ++getCount;
  }
});

assertEquals(1, localeMatcher);
assertEquals(2, style);
assertEquals(3, numeric);

// Test 1.3.2 Intl.RelativeTimeFormat.supportedLocalesOf ( locales [, options ])
var undef = Intl.RelativeTimeFormat.supportedLocalesOf();
assertEquals([], undef);

var empty = Intl.RelativeTimeFormat.supportedLocalesOf([]);
assertEquals([], empty);

var strLocale = Intl.RelativeTimeFormat.supportedLocalesOf('sr');
assertEquals('sr', strLocale[0]);

var multiLocale =
    Intl.RelativeTimeFormat.supportedLocalesOf(['sr', 'de', 'zh-CN']);
assertEquals('sr', multiLocale[0]);
assertEquals('de', multiLocale[1]);
assertEquals('zh-CN', multiLocale[2]);

var multiLocale2 =
    Intl.RelativeTimeFormat.supportedLocalesOf(['sr', 'de', 'xyzabc', 'zh-CN']);
assertEquals('sr', multiLocale2[0]);
assertEquals('de', multiLocale2[1]);
assertEquals('zh-CN', multiLocale2[2]);
