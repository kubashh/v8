// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --harmony_intl_enumeration

let calendars = Intl.getSupportedCalendars();

calendars.forEach(function(calendar) {
  assertDoesNotThrow(() => {new Intl.DateTimeFormat("en", {calendar})});
});
