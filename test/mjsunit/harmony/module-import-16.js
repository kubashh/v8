// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --harmony-dynamic-import

var ran = false;
var x;

var func = new Function("import('modules-skip-1.js').then(ns => { x = ns.life(); ran = true; } ).catch(err => %AbortJS(err))");
func();

%RunMicrotasks();
assertEquals(42, x);
assertTrue(ran);
