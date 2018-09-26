// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

var str = new String('ott');
assertEquals(['o', 't', 't'], [...str]);

str[Symbol.iterator] =
  new Function ('return { next : () => ({ value : undefined, done : true }) };');

assertEquals([], [...str]);


var str2 = new String('ott');
assertEquals(['o', 't', 't'], [...str2]);

str2.__proto__[Symbol.iterator] =
  new Function ('return { next : () => ({ value : undefined, done : true }) };');
assertEquals([], [...str2]);
