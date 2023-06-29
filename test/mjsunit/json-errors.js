// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

function TryParse(s, message) {
  try {
    JSON.parse(s);
    assertUnreachable();
  } catch(e) {
    assertEquals(message, e.message);
  }
}

var s = `{"a\\\\b `;
TryParse(s, "Unterminated string in JSON at position 7 (line 1 column 8)");

var s = `{"a\\\\\u03A9 `;
TryParse(s, "Unterminated string in JSON at position 7 (line 1 column 8)");

var s = `{"ab `;
TryParse(s, "Unterminated string in JSON at position 5 (line 1 column 6)");

var s = `{"a\u03A9 `;
TryParse(s, "Unterminated string in JSON at position 5 (line 1 column 6)");

var s = `{"a\nb":"b"}`;
TryParse(s, "Bad control character in string literal in JSON " +
            "at position 3 (line 1 column 4)");

var s = `{"a\nb":"b\u03A9"}`;
TryParse(s, "Bad control character in string literal in JSON " +
            "at position 3 (line 1 column 4)");

var s = `{\n7:1}`;
TryParse(s, "Expected property name or '}' in JSON " +
            "at position 2 (line 2 column 1)");

var s = `\n  { \n\n"12345": 5,,}`;
TryParse(s, "Expected double-quoted property name in JSON " +
            "at position 18 (line 4 column 12)");
