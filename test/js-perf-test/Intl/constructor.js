// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
function NewIntlCollator() {
  let obj = new Intl.Collator();
}
createSuite('NewIntlCollator', 100000, NewIntlCollator, ()=>{});

function NewIntlDateTimeFormat() {
  let obj = new Intl.DateTimeFormat();
}
createSuite('NewIntlDateTimeFormat', 100000, NewIntlDateTimeFormat, ()=>{});

function NewIntlNumberFormat() {
  let obj = new Intl.NumberFormat();
}
createSuite('NewIntlNumberFormat', 100000, NewIntlNumberFormat, ()=>{});

function NewIntlPluralRules() {
  let obj = new Intl.PluralRules();
}
createSuite('NewIntlPluralRules', 100000, NewIntlPluralRules, ()=>{});

function NewIntlListFormat() {
  let obj = new Intl.ListFormat();
}
createSuite('NewIntlListFormat', 100000, NewIntlListFormat, ()=>{});

function NewIntlRelativeTimeFormat() {
  let obj = new Intl.RelativeTimeFormat();
}
createSuite('NewIntlRelativeTimeFormat', 100000, NewIntlRelativeTimeFormat, ()=>{});
