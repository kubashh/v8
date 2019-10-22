// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --harmony-intl-add-calendar-numbering-system

let o1 = (new Intl.RelativeTimeFormat("en")).resolvedOptions();
assertEquals(undefined, o1.numberingSystem);

let o2 = (new Intl.RelativeTimeFormat("en-u-nu-arab")).resolvedOptions();
assertEquals("arab", o2.numberingSystem);
assertEquals("en-u-nu-arab", o2.locale);

let o3 = (new Intl.RelativeTimeFormat(
    "en-u-nu-arab", {numberingSystem: 'thai'})).resolvedOptions();
assertEquals("thai", o3.numberingSystem);
assertEquals("en", o3.locale);

let o4 = (new Intl.RelativeTimeFormat(
    "en", {numberingSystem: 'bali'})).resolvedOptions();
assertEquals("bali", o4.numberingSystem);
assertEquals("en", o4.locale);
