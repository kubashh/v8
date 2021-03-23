// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --harmony_intl_best_fit_matcher

const intl_objects = [
  Intl.Collator,
  Intl.DateTimeFormat,
  Intl.DisplayNames,
  Intl.ListFormat,
  Intl.NumberFormat,
  Intl.PluralRules,
  Intl.RelativeTimeFormat,
  Intl.Segmenter,
];

intl_objects.forEach(f => {
  f.supportedLocalesOf(["en"]);
});
