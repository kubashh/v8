// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --harmony_intl_enumeration

let currencies = Intl.getSupportedCurrencies();

currencies.forEach(function(currency) {
  assertEquals(
      currency,
      (new Intl.NumberFormat("en", {style: 'currency', currency}))
         .resolvedOptions().currency);
});
