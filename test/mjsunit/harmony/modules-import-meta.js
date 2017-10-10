// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// MODULE
// Flags: --harmony-import-meta

assertEquals("object", typeof import.meta);
assertEquals(null, Object.getPrototypeOf(import.meta));

// This property isn't part of the spec itself but is mentioned as an example
assertMatches(/\/modules-import-meta\.js$/, import.meta.url);

import.meta.x = 42;
assertEquals(42, import.meta.x);
Object.assign(import.meta, { foo: "bar" })
assertEquals("bar", import.meta.foo);
