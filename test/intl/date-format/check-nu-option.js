// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


let invalid_nu = [
  "invalid",
  "abce",
  "finance",
  "native",
  "traditio",
];

// https://www.unicode.org/repos/cldr/tags/latest/common/bcp47/number.xml
let valid_nu= [
  "adlm",
  "ahom",
  "arab",
  "arabext",
  "bali",
  "beng",
  "bhks",
  "brah",
  "cakm",
  "cham",
  "deva",
  "fullwide",
  "gong",
  "gonm",
  "gujr",
  "guru",
  "hanidec",
  "hmng",
  "java",
  "kali",
  "khmr",
  "knda",
  "lana",
  "lanatham",
  "laoo",
  "latn",
  "lepc",
  "limb",
  "mathbold",
  "mathdbl",
  "mathmono",
  "mathsanb",
  "mathsans",
  "mlym",
  "modi",
  "mong",
  "mroo",
  "mtei",
  "mymr",
  "mymrshan",
  "mymrtlng",
  "newa",
  "nkoo",
  "olck",
  "orya",
  "osma",
  "rohg",
  "saur",
  "shrd",
  "sind",
  "sinh",
  "sora",
  "sund",
  "takr",
  "talu",
  "tamldec",
  "telu",
  "thai",
  "tirh",
  "tibt",
  "vaii",
  "wara",
];

let not_working_nu= [
  "armn",
  "armnlow",
  "cyrl",
  "ethi",
  "geor",
  "grek",
  "greklow",
  "hanidays",
  "hans",
  "hansfin",
  "hant",
  "hantfin",
  "hebr",
  "jpan",
  "jpanfin",
  "roman",
  "romanlow",
  "taml",
];

let locales = [
  "en",
  "ar",
];


invalid_nu.forEach(function(nu) {
  let df = new Intl.DateTimeFormat(["en-u-nu-" + nu + "-fo-obar"]);
  assertEquals("en", df.resolvedOptions().locale);
}
);

valid_nu.forEach(function(nu) {
  locales.forEach(function(base) {
    let l = base + "-u-nu-" + nu;
    let df = new Intl.DateTimeFormat([l + "-fo-obar"]);
    assertEquals(l, df.resolvedOptions().locale);
  });
}
);

not_working_nu.forEach(function(nu) {
  locales.forEach(function(base) {
    let l = base + "-u-nu-" + nu;
    let df = new Intl.DateTimeFormat([l + "-fo-obar"]);
    assertEquals(l, df.resolvedOptions().locale);
  });
}
);
