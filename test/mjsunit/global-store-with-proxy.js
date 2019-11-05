// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

var set_count = 0;
var get_count = 0;
var has_count = 0;
var property_descriptor_count = 0;
globalThis.__proto__ = new Proxy({},
                                 {get() {get_count++},
                                  has() {has_count++;},
                                  set() {set_count++;},
                                  getOwnPropertyDescriptor() {property_desciptor_count++}});
function checkCounts(count) {
  assertEquals(has_count, count);
  assertEquals(set_count, count);
  assertEquals(get_count, 0);
  assertEquals(property_descriptor_count, 0);
}

// Check lookup_global and global calls both has and set when has returns
// false.
function store_lookup_global_has_returns_false() {
  eval("var b = 10");
  return x0 = 10;
}
assertEquals(store_lookup_global_has_returns_false(), 10);
checkCounts(1);

%EnsureFeedbackVectorForFunction(store_lookup_global_has_returns_false);
assertEquals(store_lookup_global_has_returns_false(), 10);
checkCounts(2);
assertEquals(store_lookup_global_has_returns_false(), 10);
checkCounts(3);

function store_global_has_returns_false(n) {
  return x1 = 10;
}
assertEquals(store_global_has_returns_false(), 10);
checkCounts(4);

%EnsureFeedbackVectorForFunction(store_global_has_returns_false);
assertEquals(store_global_has_returns_false(), 10);
checkCounts(5);
assertEquals(store_global_has_returns_false(), 10);
checkCounts(6);

// Check that we haven't added a property
Object.setPrototypeOf(globalThis, null);
assertEquals("undefined", typeof(x0));
assertEquals("undefined", typeof(x1));

// Check lookup_global and global calls both has and set when has returns
// true.
get_count = 0;
has_count = 0;
set_count = 0;
property_descriptor_count = 0;

var proxy = new Proxy({}, {get() {get_count++;},
                           has() {has_count++; return true;},
                           set() {set_count++; return true; },
                           getOwnPropertyDescriptor() {property_desciptor_count++}});
Object.setPrototypeOf(globalThis, proxy);

function store_lookup_global() {
  eval("var b = 10");
  return x0 = 10;
}
assertEquals(store_lookup_global(), 10);
checkCounts(1);

%EnsureFeedbackVectorForFunction(store_lookup_global);
assertEquals(store_lookup_global(), 10);
checkCounts(2);
assertEquals(store_lookup_global(), 10);
checkCounts(3);

function store_global() {
  return x1 = 10;
}

assertEquals(store_global(), 10);
checkCounts(4);

%EnsureFeedbackVectorForFunction(store_global);
assertEquals(store_global(), 10);
checkCounts(5);
assertEquals(store_global(), 10);
checkCounts(6);

// Check that we haven't added a property
Object.setPrototypeOf(globalThis, null);
assertEquals("undefined", typeof(x0));
assertEquals("undefined", typeof(x1));

get_count = 0;
has_count = 0;
set_count = 0;

// Check lookup_global and global calls has and we set a value on global
// when there is no set trap.
proxy = new Proxy({}, {has() {has_count++; return true;},
                       getOwnPropertyDescriptor() {property_desciptor_count++}});
Object.setPrototypeOf(globalThis, proxy);

function checkCountsNoSet(count) {
  assertEquals(has_count, count);
  assertEquals(set_count, 0);
  assertEquals(get_count, 0);
  assertEquals(property_descriptor_count, 0);
}

function store_lookup_global_no_set() {
  eval("var b = 10");
  return x0 = 10;
}

assertEquals(store_lookup_global_no_set(), 10);
checkCountsNoSet(1);

%EnsureFeedbackVectorForFunction(store_lookup_global_no_set);
delete x0;
assertEquals(store_lookup_global_no_set(), 10);
checkCountsNoSet(2);
delete x0;
assertEquals(store_lookup_global_no_set(), 10);
checkCountsNoSet(3);

function store_global_no_set() {
  return x1 = 10;
}

has_count = 3;
assertEquals(store_global_no_set(), 10);
checkCountsNoSet(4);

%EnsureFeedbackVectorForFunction(store_global_no_set);
delete x1;
assertEquals(store_global_no_set(), 10);
checkCountsNoSet(5);
delete x1;
assertEquals(store_global_no_set(), 10);
checkCountsNoSet(6);

Object.setPrototypeOf(globalThis, null);
assertEquals("number", typeof(x0));
assertEquals("number", typeof(x1));
delete x0;
delete x1;


// Check strict mode throws when has returns false.
proxy = new Proxy({}, {has() {has_count++; return false;},
                       getOwnPropertyDescriptor() {property_desciptor_count++}});
Object.setPrototypeOf(globalThis, proxy);

function store_global_strict() {
  'use strict';
  return x0 = 10;
}
assertThrows(store_global_strict, ReferenceError);

%EnsureFeedbackVectorForFunction(store_global_strict);
assertThrows(store_global_strict, ReferenceError);
assertThrows(store_global_strict, ReferenceError);

// Check cases when has introduces a variable we are storing into.
var var_this = this;
var set_count = 0;
var proxy = new Proxy({}, {
    has() {
        Object.setPrototypeOf(var_this, null);
        x0 = 10;
        Object.setPrototypeOf(var_this, proxy);
        return false;
    },
    set() { set_count++; }
  });
Object.setPrototypeOf(var_this, proxy);
x0 = 10;
assertEquals(typeof(x0), "number");
assertEquals(set_count, 0);
x1 = 10;
assertEquals(typeof(x1), "undefined");
assertEquals(set_count, 1);

var proxy = new Proxy({}, {
    has() {
        Object.setPrototypeOf(var_this, null);
        x0 = 10;
        Object.setPrototypeOf(var_this, proxy);
        return true;
    },
    set() { set_count++; }
  });
Object.setPrototypeOf(var_this, proxy);
x0 = 10;
assertEquals(typeof(x0), "number");
assertEquals(set_count, 1);
x1 = 10;
assertEquals(typeof(x1), "undefined");
assertEquals(set_count, 2);
