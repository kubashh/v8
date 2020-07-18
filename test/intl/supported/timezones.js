// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --harmony_intl_enumeration

let timeZones = Intl.getSupportedTimeZones();

timeZones.forEach(function(timeZone) {
  assertDoesNotThrow(() => {new Intl.DateTimeFormat("en", {timeZone})});
});
