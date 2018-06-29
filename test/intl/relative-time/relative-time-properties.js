// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --harmony-locale

// Make sure that RelativeTimeFormat exposes all required properties. Those not specified
// should have undefined value.
// http://tc39.github.io/proposal-intl-relative-time/

let rtf = new Intl.RelativeTimeFormat();


// Test 1.4.2 Intl.RelativeTimeFormat.prototype[ @@toStringTag ]
// Intl.RelativeTimeFormat.prototype[ @@toStringTag ]
let descriptor =
    Object.getOwnPropertyDescriptor(Intl.RelativeTimeFormat.prototype,
                                    Symbol.toStringTag);
assertEquals('Object', descriptor.value);
assertEquals(false, descriptor.enumerable);
assertEquals(false, descriptor.writable);
assertEquals(true, descriptor.configurable);

// Test 1.4.3 Intl.RelativeTimeFormat.prototype.format( value, unit )
assertEquals('string', typeof rtf.format(-1, 'seconds'));
assertEquals('string', typeof rtf.format(-1, 'second'));
assertEquals('string', typeof rtf.format(-1, 'minutes'));
assertEquals('string', typeof rtf.format(-1, 'minute'));
assertEquals('string', typeof rtf.format(-1, 'hours'));
assertEquals('string', typeof rtf.format(-1, 'hour'));
assertEquals('string', typeof rtf.format(-1, 'days'));
assertEquals('string', typeof rtf.format(-1, 'day'));
assertEquals('string', typeof rtf.format(-1, 'weeks'));
assertEquals('string', typeof rtf.format(-1, 'week'));
assertEquals('string', typeof rtf.format(-1, 'months'));
assertEquals('string', typeof rtf.format(-1, 'month'));
assertEquals('string', typeof rtf.format(-1, 'years'));
assertEquals('string', typeof rtf.format(-1, 'year'));
//assertEquals('string', typeof rtf.format(-1, 'quarter'));
//assertEquals('string', typeof rtf.format(-1, 'quarters'));
/*
assertThrows(rtf.format(-1, 'decades'), RangeError);
assertThrows(rtf.format(-1, 'decade'), RangeError);
assertThrows(rtf.format(-1, 'centuries'), RangeError);
assertThrows(rtf.format(-1, 'century'), RangeError);
assertThrows(rtf.format(-1, 'milliseconds'), RangeError);
assertThrows(rtf.format(-1, 'millisecond'), RangeError);
assertThrows(rtf.format(-1, 'microseconds'), RangeError);
assertThrows(rtf.format(-1, 'microsecond'), RangeError);
assertThrows(rtf.format(-1, 'nanoseconds'), RangeError);
assertThrows(rtf.format(-1, 'nanosecond'), RangeError);
*/

assertEquals('string', typeof rtf.format(5, 'day'));
assertEquals('string', typeof rtf.format('5', 'day'));
assertEquals('string', typeof rtf.format('-5', 'day'));
assertEquals('string', typeof rtf.format('534', 'day'));
assertEquals('string', typeof rtf.format('-534', 'day'));

//assertThrows(rtf.format('xyz', 'day'), RangeError);

// Test 1.4.4 Intl.RelativeTimeFormat.prototype.formatToParts( value, unit )
assertEquals(true, Array.isArray(rtf.formatToParts(-1, 'seconds')));
assertEquals(true, Array.isArray(rtf.formatToParts(-1, 'second')));
assertEquals(true, Array.isArray(rtf.formatToParts(-1, 'minutes')));
assertEquals(true, Array.isArray(rtf.formatToParts(-1, 'minute')));
assertEquals(true, Array.isArray(rtf.formatToParts(-1, 'hours')));
assertEquals(true, Array.isArray(rtf.formatToParts(-1, 'hour')));
assertEquals(true, Array.isArray(rtf.formatToParts(-1, 'days')));
assertEquals(true, Array.isArray(rtf.formatToParts(-1, 'day')));
assertEquals(true, Array.isArray(rtf.formatToParts(-1, 'weeks')));
assertEquals(true, Array.isArray(rtf.formatToParts(-1, 'week')));
assertEquals(true, Array.isArray(rtf.formatToParts(-1, 'months')));
assertEquals(true, Array.isArray(rtf.formatToParts(-1, 'month')));
assertEquals(true, Array.isArray(rtf.formatToParts(-1, 'quarters')));
assertEquals(true, Array.isArray(rtf.formatToParts(-1, 'quarter')));
assertEquals(true, Array.isArray(rtf.formatToParts(-1, 'years')));
assertEquals(true, Array.isArray(rtf.formatToParts(-1, 'year')));
//assertThrows(rtf.formatToParts(-1, 'decades'), RangeError);
//assertThrows(rtf.formatToParts(-1, 'decade'), RangeError);
//assertThrows(rtf.formatToParts(-1, 'centuries'), RangeError);
//assertThrows(rtf.formatToParts(-1, 'century'), RangeError);
//assertThrows(rtf.formatToParts(-1, 'milliseconds'), RangeError);
//assertThrows(rtf.formatToParts(-1, 'millisecond'), RangeError);
//assertThrows(rtf.formatToParts(-1, 'microseconds'), RangeError);
//assertThrows(rtf.formatToParts(-1, 'microsecond'), RangeError);
//assertThrows(rtf.formatToParts(-1, 'nanoseconds'), RangeError);
//assertThrows(rtf.formatToParts(-1, 'nanosecond'), RangeError);

assertEquals(true, Array.isArray(rtf.formatToParts(100, 'day')));
rtf.formatToParts(100, 'day').forEach(function(part) {
  assertEquals(true, part.type == 'literal' || part.type == 'integer');
  assertEquals('string', typeof part.value);
  if (part.type == 'integer') {
    assertEquals('string', typeof part.unit);
  }
});

// Test 1.4.5 Intl.RelativeTimeFormat.prototype.resolvedOptions ()
// The default style is 'long'
assertEquals('long', rtf.resolvedOptions().style);

// The default numeric is 'always'
assertEquals('always', rtf.resolvedOptions().numeric);

assertEquals(
    'short',
    (new Intl.RelativeTimeFormat('sr', {style: 'short'}))
        .resolvedOptions().style);

assertEquals(
    'narrow',
    (new Intl.RelativeTimeFormat('sr', {style: 'narrow'}))
        .resolvedOptions().style);

assertEquals(
    'long',
    (new Intl.RelativeTimeFormat('sr', {style: 'long'}))
        .resolvedOptions().style);

assertEquals(
    'auto',
    (new Intl.RelativeTimeFormat('sr', {numeric: 'auto'}))
        .resolvedOptions().numeric);

assertEquals(
    'always',
    (new Intl.RelativeTimeFormat('sr', {numeric: 'always'}))
        .resolvedOptions().numeric);
