// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Flags: --harmony-temporal

d8.file.execute('test/mjsunit/temporal/temporal-helpers.js');

// Test the fields return Iterable (such as Set) instead of Array will also work
class FieldsReturnSetCalendar extends Temporal.Calendar {
  constructor(id) { super(id); }
  fields(f) {
    // Assert the pass in value is array
    assertTrue(Array.isArray(f));
    // Return Set instead of Array to test the CalendarFields handle Iterable
    return new Set(super.fields(f));
  }
}

let fieldsReturnSetCalendar = new FieldsReturnSetCalendar("iso8601");

let d1 = new Temporal.PlainDate(2021, 12, 11, fieldsReturnSetCalendar);

assertPlainYearMonth(d1.toPlainYearMonth(), 2021, 12);

// Test the fields return Iterable instead of Array will also work
class FieldsReturnIterableCalendar extends Temporal.Calendar {
  constructor(id) { super(id); }
  fields(f) {
    // Assert the pass in value is array
    assertTrue(Array.isArray(f));
    let iterable = {};
    let i = 0;
    iterable[Symbol.iterator] = function() {
      return {
         next: function() {
           if (i < f.length) {
             return {done: false, value:f[i++]};
           }
           return {done:true};
         }
      };
    };
    return iterable;
  }
}

let fieldsReturnIterableCalendar = new FieldsReturnIterableCalendar("iso8601");
let d2 = new Temporal.PlainDate(2021, 12, 11, fieldsReturnIterableCalendar);

assertPlainYearMonth(d2.toPlainYearMonth(), 2021, 12);
