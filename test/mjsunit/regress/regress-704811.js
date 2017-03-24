// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The bug was that destructuring assignments which occur inside a lazy arrow
// function parameter list were not rewritten.

// Repro from the bug (slightly modified so that it doesn't produce a run-time
// exception).
(({x = {} = {}}) => {})({});

// Testing that the destructuring assignments also work properly. The syntax is
// a bit scary: The value of the destructuring assignemnt is an object {myprop:
// 2115} and 2115 also gets assigned to side_assignment_thing. So the default
// value for x is {myprop: 2115}. This is the value which x will have if the
// function is called with an object which doesn't have property x.
let called = false;
(({x = {myprop: side_assignment_thing} = {myprop: 2115}}) => {
  assertTrue('myprop' in x);
  assertEquals(2115, x.myprop);
  assertEquals(2115, side_assignment_thing);
  called = true;
})({});
assertTrue(called);

// If the parameter is an object which has property x, the default value is not
// used.
called = false;
(({x = {myprop: side_assignment_thing} = {myprop: 2115}}) => {
  assertEquals(2115, side_assignment_thing);
  assertEquals(3000, x);
  called = true;
})({x: 3000});
assertTrue(called);

// Different kinds of lazy arrow xfunctions (it's actually a bit weird that the above
// functions are lazy, since they are parenthesized).
called = false;
let a1 = ({x = {myprop: side_assignment_thing} = {myprop: 2115}}) => {
  assertTrue('myprop' in x);
  assertEquals(2115, x.myprop);
  assertEquals(2115, side_assignment_thing);
  called = true;
}
a1({});
assertTrue(called);

called = false;
let a2 = ({x = {myprop: side_assignment_thing} = {myprop: 2115}}) => {
  assertEquals(2115, side_assignment_thing);
  assertEquals(3000, x);
  called = true;
}
a2({x: 3000});
assertTrue(called);

// We never had a problem with non-arrow functions, but testing them too for
// completeness.
called = false;
function f1({x = {myprop: side_assignment_thing} = {myprop: 2115}}) {
  assertTrue('myprop' in x);
  assertEquals(2115, x.myprop);
  assertEquals(2115, side_assignment_thing);
  called = true;
}
f1({});
assertTrue(called);

called = false;
function f2({x = {myprop: side_assignment_thing} = {myprop: 2115}}) {
  assertEquals(2115, side_assignment_thing);
  assertEquals(3000, x);
  called = true;
}
f2({x: 3000});
assertTrue(called);
