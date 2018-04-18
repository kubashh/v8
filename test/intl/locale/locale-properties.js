// Copyright 2018 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// Make sure that locale exposes all required properties. Those not specified
// should have undefined value.

let locale = new Intl.Locale('sr-cyrl-rs-t-ja-u-ca-islamic-x-whatever', {
  calendar: 'buddhist',
  caseFirst: 'true',
  collation: 'phonebk',
  currency: 'RSD',
  hourCycle: 'h23',
  caseFirst: 'upper',
  numeric: 'true',
  numberingSystem: 'roman'
});
assertEquals('sr', locale.language);
assertEquals('Cyrl', locale.script);
assertEquals('RS', locale.region);
assertEquals('sr-Cyrl-RS', locale.baseName);
assertEquals('buddhist', locale.calendar);
assertEquals('phonebk', locale.collation);
assertEquals('rsd', locale.currency);
assertEquals('h23', locale.hourCycle);
assertEquals('upper', locale.caseFirst);
assertEquals(true, locale.numeric);
assertEquals('roman', locale.numberingSystem);
assertEquals(undefined, locale.timeZone);

// Locale with the time zone.
let localeTZ = new Intl.Locale('de-u-tz-uslax');
assertEquals('de', localeTZ.language);
assertEquals('de', localeTZ.baseName);
assertEquals('uslax', localeTZ.timeZone);
