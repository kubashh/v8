// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Environment Variables: TZ=Europe/London
//
// Test in 1847 the timezone offset return 1.25 in the timezone Europe/London
assertEquals(1.25, (new Date( 1847, 11, 0)).getTimezoneOffset());
