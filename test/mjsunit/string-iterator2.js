// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

var str = new String('ott');
assertEquals(['o', 't', 't'], [...str]);

var next = function() {
             return { next : () => ({ value : undefined, done : true }) };
           };

str[Symbol.iterator] = next;
assertEquals([], [...str]);


var str2 = new String('ott');
assertEquals(['o', 't', 't'], [...str2]);

str2.__proto__[Symbol.iterator] = next;
assertEquals([], [...str2]);
