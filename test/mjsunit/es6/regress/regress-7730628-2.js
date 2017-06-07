// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --no-lazy

var y;
var destructuringArrowOSR = ({ x } = { y } = { x: 1, y: 2 }) => {
  for (var i = 0; i < 1000000; i++);
};
destructuringArrowOSR();
