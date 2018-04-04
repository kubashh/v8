// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

Debug = debug.Debug;

// StaNamedProperty
var a = {name: 'foo'};
function set_name(a) {
  a.name = 'bar';
  return a.name;
}

fail(`set_name(a)`);
success('bar', `set_name({name: 'foo'})`);

// StaNamedOwnProperty
var name_value = 'value';
function create_object_literal() {
  var obj = {name: name_value};
  return obj.name;
};

success('value', `create_object_literal()`);

// StaKeyedProperty
var arrayValue = 1;
function create_array_literal() {
  return [arrayValue];
}
var b = { 1: 2 };

success([arrayValue], `create_array_literal()`)
fail(`b[1] ^= 2`);

// StaInArrayLiteral
function return_array_use_spread(a) {
  return [...a];
}

fail(`return_array_use_spread([1])`);

// StaDataPropertyInLiteral
function return_literal_with_data_property(a) {
  return {[a] : 1};
}

success({foo: 1}, `return_literal_with_data_property('foo')`);

function success(expectation, source) {
  const result = Debug.evaluateGlobal(source, true).value();
  if (expectation !== undefined) assertEquals(expectation, result);
}

function fail(source) {
  assertThrows(() => Debug.evaluateGlobal(source, true),
               EvalError);
}
