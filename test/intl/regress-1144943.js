// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --expose-gc --verify-heap
// Test Direct-leak in _Block_copy

(function () {
  let countGC = 0;
    if (countGC++ < 50) { gc(); }
})();
try {
  if (found === expected) { } else try { } catch (e) {}
  for (var __v_11 = 0; __v_11 < __v_7.length; ++__v_11) { ; }
} catch (e) {}
var __v_0 = {};
try { } catch (e) {}
{ try { } catch (e) {} }
try { ; } catch (e) {}
function __f_9() { }
__v_39 = 2147483647;
__v_40 = new Uint8ClampedArray(__v_39);
try { __v_41 = new Intl.DateTimeFormat(); } catch (e) {} ;
