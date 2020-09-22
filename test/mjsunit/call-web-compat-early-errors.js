// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Will be used in the tests
function foo() {}

function wrapInLazyFunction(s) {
  return "function test() { " + s + "}";
}

function assertEarlyError(s) {
  assertThrows(wrapInLazyFunction(s), SyntaxError);
}

function assertLateError(s) {
  assertDoesNotThrow(wrapInLazyFunction(s));
  assertThrows(s, ReferenceError);
}

// Web compatibility:
assertLateError("foo()++;");
assertLateError("foo()--;");
assertLateError("++foo();");
assertLateError("--foo();");
assertLateError("foo() = 1;");
assertLateError("foo() += 1;");
assertLateError("foo() -= 1;");
assertLateError("foo() *= 1;");
assertLateError("foo() /= 1;");
assertLateError("for (foo() = 0; ; ) {}");

// Modern language features:
// Tagged templates
assertEarlyError("foo() `foo` ++;");
assertEarlyError("foo() `foo` --;");
assertEarlyError("++ foo() `foo`;");
assertEarlyError("-- foo() `foo`;");
assertEarlyError("foo() `foo` = 1;");
assertEarlyError("foo() `foo` += 1;");
assertEarlyError("foo() `foo` -= 1;");
assertEarlyError("foo() `foo` *= 1;");
assertEarlyError("foo() `foo` /= 1;");
assertEarlyError("for (foo() `foo` = 0; ; ) {}");

// Logical assignment
assertEarlyError("foo() &&= 1;");
assertEarlyError("foo() ||= 1;");
assertEarlyError("foo() ??= 1;");

// For of / in
assertEarlyError("for (foo() of []) {}");
assertEarlyError("for (foo() in {}) {}");

// For await
assertEarlyError("for await (foo() = 0; ;  {}");
assertEarlyError("for await (foo() of []) {}");
assertEarlyError("for await (foo() in {}) {}");
