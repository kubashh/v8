// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

var str = new String('ott');

assertEquals(['o', 't', 't'], [...str]);

// This changes the String Iterator prototype. No more test should be run after
// this in the same instance.
var iterator = str[Symbol.iterator]();
iterator.__proto__.next = () => ({ value : undefined, done : true });

assertEquals([], [...str]);
