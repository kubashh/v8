// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

var proxy = {};
var key_or_proxy = 0;

function failing_get() {
  var foo = proxy[key_or_proxy];
}


// Tail call proxy function when caller has an arguments adaptor frame.
assertThrows(function test() {
  function func1() {
  }
  try { proxy = new Proxy(func1, {}); } catch(e) { }

  failing_get();

  function func2() {
  }
  try { key_or_proxy = new Proxy(func2, {}); } catch(e) { }

  failing_get();
}, TypeError);
