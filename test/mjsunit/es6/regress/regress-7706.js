// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

class Test extends Number {}
Test.prototype[Symbol.toStringTag] = "Test";

function test(o, expected, desc) {
  %ToFastProperties(o.__proto__);
  assertEquals(expected, Object.prototype.toString.call(o), desc);
}

test(new Test, "[object Test]", "Try #1");
test(new Test, "[object Test]", "Try #2");
