// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

function assert_throws(code, func, description) {
  try {
    func();
  } catch (e) {
    assert_true(
        e.name === code.name,
        'expected exception ' + code.name + ', got ' + e.name);
    return;
  }
  assert_true(
      false, 'expected exception ' + code.name + ', no exception thrown');
}

function promise_rejects(test, constructor, promise, description) {
  return promise_rejects_js(test, constructor, promise, description);
}
