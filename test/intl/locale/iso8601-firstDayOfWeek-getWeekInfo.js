// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Flags: --harmony-intl-locale-info-func

// Ensure the fw work under iso8601 calendar
assertEquals(1, (new Intl.Locale("en-u-ca-iso8601-fw-mon")).getWeekInfo().firstDay);
assertEquals(2, (new Intl.Locale("en-u-ca-iso8601-fw-tue")).getWeekInfo().firstDay);
assertEquals(3, (new Intl.Locale("en-u-ca-iso8601-fw-wed")).getWeekInfo().firstDay);
assertEquals(4, (new Intl.Locale("en-u-ca-iso8601-fw-thu")).getWeekInfo().firstDay);
assertEquals(5, (new Intl.Locale("en-u-ca-iso8601-fw-fri")).getWeekInfo().firstDay);
assertEquals(6, (new Intl.Locale("en-u-ca-iso8601-fw-sat")).getWeekInfo().firstDay);
assertEquals(7, (new Intl.Locale("en-u-ca-iso8601-fw-sun")).getWeekInfo().firstDay);
