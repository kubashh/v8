// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

var x = new Intl.Collator('de-x-u-co-emoji');
var { collation } = x.resolvedOptions();
assertEquals('default', collation);

var x = new Intl.Collator('de-u-kn-true-x-u-co-emoji');
var { numeric, collation } = x.resolvedOptions();
assertEquals('default', collation);
assertEquals(true, numeric);

var x = new Intl.Collator('de-u-kn-true-x-u-kn-false');
var { numeric, collation } = x.resolvedOptions();
assertEquals(true, numeric);
