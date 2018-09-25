// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

var i = 0;
new Intl.DateTimeFormat(
    undefined,
    {
      hour12: undefined,
      weekday: undefined,
      era: undefined,
      year: undefined,
      month: undefined,
      day: undefined,
      minute: undefined,
      second: undefined,
      timeZoneName: undefined,
      get hour() {  if (i == 0) { i = 1; return 'numeric'} }
    });
