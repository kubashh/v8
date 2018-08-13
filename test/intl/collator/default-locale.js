// Copyright 2013 the V8 project authors. All rights reserved.
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

// Constructing Collator with no locale arguments or with []
// creates one with default locale.

var collator = new Intl.Collator([]);

var options = collator.resolvedOptions();

// Check it's none of these first.
assertFalse(options.locale === 'und');
assertFalse(options.locale === '');
assertFalse(options.locale === undefined);

// Then check for legitimacy.
assertLanguageTag(%GetDefaultICULocale(), options.locale);

var collatorNone = new Intl.Collator();
assertEquals(options.locale, collatorNone.resolvedOptions().locale);

// TODO(cira): remove support for {} to mean empty list.
var collatorBraket = new Intl.Collator({});
assertEquals(options.locale, collatorBraket.resolvedOptions().locale);

// No locale
var collatorWithOptions = new Intl.Collator(undefined);
var { locale, usage, collation } = collatorWithOptions.resolvedOptions();
assertLanguageTag(%GetDefaultICULocale(), locale);
assertEquals('sort', usage);
assertEquals('default', collation);
assertEquals(locale.indexOf('-co-search'), -1);

collatorWithOptions = new Intl.Collator(undefined, {usage: 'sort'});
var { locale, usage, collation } = collatorWithOptions.resolvedOptions();
assertLanguageTag(%GetDefaultICULocale(), locale);
assertEquals('sort', usage);
assertEquals('default', collation);
assertEquals(locale.indexOf('-co-search'), -1);

collatorWithOptions = new Intl.Collator(undefined, {usage: 'search'});
var { locale, usage, collation } = collatorWithOptions.resolvedOptions();
assertEquals('search', usage);
assertEquals('default', collation);
assertLanguageTag(%GetDefaultICULocale(), locale);

// As per the spec, V8 shouldn't add the 'co-search' unicode extension
// to the language tag, when the extension value is search or
// standard. But, deleting it from the language tag returned by ICU is
// expensive as we have to reparse the tag and trim it, or create a
// new ICU locale class without the 'co' unicode extension.
//
// Since V8 will anyway not consider the 'co-search' value if passed
// in as an extension, I think it's fine to digress from the spec
// here.
assertEquals(locale.indexOf('-co-search'), 7);

collatorWithOptions = new Intl.Collator(locale);
var { locale, usage, collation } = collatorWithOptions.resolvedOptions();
assertLanguageTag(%GetDefaultICULocale(), locale);
assertEquals('sort', usage);
assertEquals('default', collation);
assertEquals(locale.indexOf('-co-search'), -1);

// With Locale
collatorWithOptions = new Intl.Collator('en-US');
var { locale, usage, collation } = collatorWithOptions.resolvedOptions();
assertLanguageTag(%GetDefaultICULocale(), locale);
assertEquals('sort', usage);
assertEquals('default', collation);
assertEquals(locale.indexOf('-co-search'), -1);

collatorWithOptions = new Intl.Collator('en-US', {usage: 'sort'});
var { locale, usage, collation } = collatorWithOptions.resolvedOptions();
assertLanguageTag(%GetDefaultICULocale(), locale);
assertEquals('sort', usage);
assertEquals('default', collation);
assertEquals(locale.indexOf('-co-search'), -1);

collatorWithOptions = new Intl.Collator('en-US', {usage: 'search'});
var { locale, usage, collation } = collatorWithOptions.resolvedOptions();
assertEquals('search', usage);
assertEquals('default', collation);
assertLanguageTag(%GetDefaultICULocale(), locale);

// With invalid collation value = 'search'
collatorWithOptions = new Intl.Collator('en-US-u-co-search');
var { locale, usage, collation } = collatorWithOptions.resolvedOptions();
assertLanguageTag(%GetDefaultICULocale(), locale);
assertEquals('sort', usage);
assertEquals('default', collation);
assertEquals(locale.indexOf('-co-search'), -1);

collatorWithOptions = new Intl.Collator('en-US-u-co-search', {usage: 'sort'});
var { locale, usage, collation } = collatorWithOptions.resolvedOptions();
assertLanguageTag(%GetDefaultICULocale(), locale);
assertEquals('sort', usage);
assertEquals('default', collation);
assertEquals(locale.indexOf('-co-search'), -1);

collatorWithOptions = new Intl.Collator('en-US-u-co-search', {usage: 'search'});
var { locale, usage, collation } = collatorWithOptions.resolvedOptions();
assertLanguageTag(%GetDefaultICULocale(), locale);
assertEquals('search', usage);
assertEquals('default', collation);

// With invalid collation value = 'standard'
collatorWithOptions = new Intl.Collator('en-US-u-co-standard');
var { locale, usage, collation } = collatorWithOptions.resolvedOptions();
assertLanguageTag(%GetDefaultICULocale(), locale);
assertEquals('sort', usage);
assertEquals('default', collation);
assertEquals(locale.indexOf('-co-search'), -1);

collatorWithOptions = new Intl.Collator('en-US-u-co-standard', {usage: 'sort'});
var { locale, usage, collation } = collatorWithOptions.resolvedOptions();
assertLanguageTag(%GetDefaultICULocale(), locale);
assertEquals('sort', usage);
assertEquals('default', collation);
assertEquals(locale.indexOf('-co-search'), -1);

collatorWithOptions = new Intl.Collator('en-US-u-co-standard', {usage: 'search'});
var { locale, usage, collation } = collatorWithOptions.resolvedOptions();
assertLanguageTag(%GetDefaultICULocale(), locale);
assertEquals('search', usage);
assertEquals('default', collation);

// With valid collation value = 'emoji'
collatorWithOptions = new Intl.Collator('en-US-u-co-emoji');
var { locale, usage, collation } = collatorWithOptions.resolvedOptions();
assertLanguageTag(%GetDefaultICULocale(), locale);
assertEquals('sort', usage);
assertEquals('emoji', collation);
assertEquals(locale.indexOf('-co-search'), -1);

collatorWithOptions = new Intl.Collator('en-US-u-co-emoji', {usage: 'sort'});
var { locale, usage, collation } = collatorWithOptions.resolvedOptions();
assertLanguageTag(%GetDefaultICULocale(), locale);
assertEquals('sort', usage);
assertEquals('emoji', collation);
assertEquals(locale.indexOf('-co-search'), -1);

collatorWithOptions = new Intl.Collator('en-US-u-co-emoji', {usage: 'search'});
var { locale, usage, collation } = collatorWithOptions.resolvedOptions();
assertLanguageTag(%GetDefaultICULocale(), locale);
assertEquals('search', usage);
// usage = search overwrites emoji as a collation value.
assertEquals('default', collation);
