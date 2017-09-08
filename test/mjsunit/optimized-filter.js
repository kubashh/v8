// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --expose-gc --turbo-inline-array-builtins
// Flags: --opt --no-always-opt

var a = [0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,0,0];
var b = [1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25];
var c = [1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25];

// Unknown field access leads to soft-deopt unrelated to map, should still
// lead to correct result.
(function() {
  var result = 0;
  var eagerDeoptInCalled = function(deopt) {
    var callback = function(v,i,o) {
      result += v;
      if (i == 13 && deopt) {
        a.abc = 25;
      }
      return true;
    }
    a.filter(callback);
  }
  eagerDeoptInCalled();
  eagerDeoptInCalled();
  %OptimizeFunctionOnNextCall(eagerDeoptInCalled);
  eagerDeoptInCalled();
  eagerDeoptInCalled(true);
  eagerDeoptInCalled();
  assertEquals(1500, result);
})();

// Length change detected during loop, must cause properly handled eager deopt.
(function() {
  var result = 0;
  var eagerDeoptInCalled = function(deopt) {
    var callback = function(v,i,o) {
      result += v;
      a.length = (i == 13 && deopt) ? 25 : 27;
      return true;
    }
    a.filter(callback);
  }
  eagerDeoptInCalled();
  eagerDeoptInCalled();
  %OptimizeFunctionOnNextCall(eagerDeoptInCalled);
  eagerDeoptInCalled();
  eagerDeoptInCalled(true);
  eagerDeoptInCalled();
  assertEquals(1500, result);
})();

// Escape analyzed array
(function() {
  var result = 0;
  var eagerDeoptInCalled = function(deopt) {
    var a_noescape = [0,1,2,3,4,5];
    var callback = function(v,i,o) {
      result += v;
      if (i == 13 && deopt) {
        a_noescape.length = 25;
      }
      return true;
    }
    a_noescape.filter(callback);
  }
  eagerDeoptInCalled();
  eagerDeoptInCalled();
  %OptimizeFunctionOnNextCall(eagerDeoptInCalled);
  eagerDeoptInCalled();
  eagerDeoptInCalled(true);
  eagerDeoptInCalled();
  assertEquals(75, result);
})();

// Escape analyzed array where callback function isn't inlined, forcing a lazy
// deopt with GC that relies on the stashed-away return result fro the lazy
// deopt being properly stored in a place on the stack that gets GC'ed.
(function() {
  var result = 0;
  var lazyDeopt = function(deopt) {
    var b = [1,2,3];
    var callback = function(v,i,o) {
      result += i;
      if (i == 1 && deopt) {
        %DeoptimizeFunction(lazyDeopt);
      }
      gc(); gc();
      return true;
    };
    %NeverOptimizeFunction(callback);
    b.filter(callback);
  }
  lazyDeopt();
  lazyDeopt();
  %OptimizeFunctionOnNextCall(lazyDeopt);
  lazyDeopt();
  lazyDeopt(true);
  lazyDeopt();
})();

// Lazy deopt from runtime call from inlined callback function.
(function() {
  var result = 0;
  var lazyDeopt = function(deopt) {
    var callback = function(v,i,o) {
      result += i;
      if (i == 13 && deopt) {
          %DeoptimizeNow();
      }
      return true;
    }
    b.filter(callback);
  }
  lazyDeopt();
  lazyDeopt();
  %OptimizeFunctionOnNextCall(lazyDeopt);
  lazyDeopt();
  lazyDeopt(true);
  lazyDeopt();
  assertEquals(1500, result);
})();

// Lazy deopt from runtime call from non-inline callback function.
(function() {
  var result = 0;
  var lazyDeopt = function(deopt) {
    var callback = function(v,i,o) {
      result += i;
      if (i == 13 && deopt) {
          %DeoptimizeNow();
      }
      return true;
    };
    %NeverOptimizeFunction(callback);
    b.filter(callback);
  }
  lazyDeopt();
  lazyDeopt();
  %OptimizeFunctionOnNextCall(lazyDeopt);
  lazyDeopt();
  lazyDeopt(true);
  lazyDeopt();
  assertEquals(1500, result);
})();

(function() {
  var result = 0;
  var lazyDeopt = function(deopt) {
    var callback = function(v,i,o) {
      result += i;
      if (i == 13 && deopt) {
          %DeoptimizeNow();
          gc();
          gc();
          gc();
      }
      return true;
    }
    c.filter(callback);
  }
  lazyDeopt();
  lazyDeopt();
  %OptimizeFunctionOnNextCall(lazyDeopt);
  lazyDeopt();
  lazyDeopt(true);
  lazyDeopt();
  assertEquals(1500, result);
})();

// Call to a.map is done inside a try-catch block and the callback function
// being called actually throws.
(function() {
  var caught = false;
  var result = 0;
  var lazyDeopt = function(deopt) {
    var callback = function(v,i,o) {
      result += i;
      if (i == 1 && deopt) {
        throw("a");
      }
      return true;
    }
    try {
      c.filter(callback);
    } catch (e) {
      caught = true;
    }
  }
  lazyDeopt();
  lazyDeopt();
  %OptimizeFunctionOnNextCall(lazyDeopt);
  lazyDeopt();
  assertDoesNotThrow(() => lazyDeopt(true));
  assertTrue(caught);
  lazyDeopt();
})();

// Call to a.map is done inside a try-catch block and the callback function
// being called actually throws, but the callback is not inlined.
(function() {
  var caught = false;
  var result = 0;
  var lazyDeopt = function(deopt) {
    var callback = function(v,i,o) {
      result += i;
      if (i == 1 && deopt) {
        throw("a");
      }
      return true;
    };
    %NeverOptimizeFunction(callback);
    try {
      c.filter(callback);
    } catch (e) {
      caught = true;
    }
  }
  lazyDeopt();
  lazyDeopt();
  %OptimizeFunctionOnNextCall(lazyDeopt);
  lazyDeopt();
  assertDoesNotThrow(() => lazyDeopt(true));
  assertTrue(caught);
  lazyDeopt();
})();

// Call to a.map is done inside a try-catch block and the callback function
// being called throws into a deoptimized caller function.
(function TestThrowIntoDeoptimizedOuter() {
  var a = [1,2,3,4];
  var lazyDeopt = function(deopt) {
    var callback = function(v,i,o) {
      if (i == 1 && deopt) {
        %DeoptimizeFunction(lazyDeopt);
        throw "some exception";
      }
      return true;
    };
    %NeverOptimizeFunction(callback);
    var result = 0;
    try {
      result = a.filter(callback);
    } catch (e) {
      assertEquals("some exception", e)
      result = "nope";
    }
    return result;
  }
  assertEquals([1,2,3,4], lazyDeopt(false));
  assertEquals([1,2,3,4], lazyDeopt(false));
  assertEquals("nope", lazyDeopt(true));
  assertEquals("nope", lazyDeopt(true));
  %OptimizeFunctionOnNextCall(lazyDeopt);
  assertEquals([1,2,3,4], lazyDeopt(false));
  assertEquals("nope", lazyDeopt(true));
})();

// An error generated inside the callback includes filter in it's
// stack trace.
(function() {
  var re = /Array\.filter/;
  var lazyDeopt = function(deopt) {
    var b = [1,2,3];
    var result = 0;
    var callback = function(v,i,o) {
      result += v;
      if (i == 1) {
        var e = new Error();
        assertTrue(re.exec(e.stack) !== null);
      }
      return true;
    };
    var o = [1,2,3];
    b.filter(callback);
  }
  lazyDeopt();
  lazyDeopt();
  %OptimizeFunctionOnNextCall(lazyDeopt);
  lazyDeopt();
})();

// An error generated inside a non-inlined callback function also
// includes filter in it's stack trace.
(function() {
  var re = /Array\.filter/;
  var lazyDeopt = function(deopt) {
    var b = [1,2,3];
    var result = 0;
    var callback = function(v,i,o) {
      result += v;
      if (i == 1) {
        var e = new Error();
        assertTrue(re.exec(e.stack) !== null);
      }
      return true;
    };
    %NeverOptimizeFunction(callback);
    var o = [1,2,3];
    b.filter(callback);
  }
  lazyDeopt();
  lazyDeopt();
  %OptimizeFunctionOnNextCall(lazyDeopt);
  lazyDeopt();
})();

// An error generated inside a recently deoptimized callback function
// includes filter in it's stack trace.
(function() {
  var re = /Array\.filter/;
  var lazyDeopt = function(deopt) {
    var b = [1,2,3];
    var result = 0;
    var callback = function(v,i,o) {
      result += v;
      if (i == 1) {
        %DeoptimizeNow();
      } else if (i == 2) {
        var e = new Error();
        assertTrue(re.exec(e.stack) !== null);
      }
      return true;
    };
    var o = [1,2,3];
    b.filter(callback);
  }
  lazyDeopt();
  lazyDeopt();
  %OptimizeFunctionOnNextCall(lazyDeopt);
  lazyDeopt();
})();

// Verify that various exception edges are handled appropriately.
// The thrown Error object should always indicate it was created from
// a filter call stack.
(function() {
  var re = /Array\.filter/;
  var a = [1,2,3];
  var result = 0;
  var lazyDeopt = function() {
    var callback = function(v,i,o) {
      result += i;
      if (i == 1) {
        %DeoptimizeFunction(lazyDeopt);
        throw new Error();
      }
      return true;
    };
    a.filter(callback);
  }
  assertThrows(() => lazyDeopt());
  assertThrows(() => lazyDeopt());
  try {
    lazyDeopt();
  } catch (e) {
    assertTrue(re.exec(e.stack) !== null);
  }
  %OptimizeFunctionOnNextCall(lazyDeopt);
  try {
    lazyDeopt();
  } catch (e) {
    assertTrue(re.exec(e.stack) !== null);
  }
})();

/*
// Verify that we remain in optimized code despite transitions in the output
// array.
(function() {
  var result = 0;
  var to_double = function() {
    var callback = function(v,i,o) {
      result += v;
      if (i < 5) {
        // First transition the output array to PACKED_DOUBLE_ELEMENTS.
        return v + 0.5;
      } else {
        // Then return smi values and make sure they can live in the double
        // array.
        return v;
      }
    }
    return c.map(callback);
  }
  to_double();
  to_double();
  %OptimizeFunctionOnNextCall(to_double);
  var output = to_double();
  assertTrue(%HasDoubleElements(output));
  assertEquals(1.5, output[0]);
  assertEquals(6, output[5]);
  assertEquals(975, result);
  assertOptimized(to_double);
})();

(function() {
  var result = 0;
  var to_fast = function() {
    var callback = function(v,i,o) {
      result += v;
      if (i < 5) {
        // First transition the output array to PACKED_DOUBLE_ELEMENTS.
        return v + 0.5;
      } else if (i < 10) {
        // Then return smi values and make sure they can live in the double
        // array.
        return v;
      } else {
        // Later, to PACKED_ELEMENTS.
        return v + 'hello';
      }
    }
    return c.map(callback);
  }
  to_fast();
  to_fast();
  %OptimizeFunctionOnNextCall(to_fast);
  var output = to_fast();
  %HasObjectElements(output);
  assertEquals(975, result);
  assertEquals("11hello", output[10]);
  assertOptimized(to_fast);
})();
*/

// Messing with the Array species constructor causes deoptimization.
(function() {
  var result = 0;
  var a = [1,2,3];
  var species_breakage = function() {
    var callback = function(v,i,o) {
      result += v;
      return true;
    }
    a.filter(callback);
  }
  species_breakage();
  species_breakage();
  %OptimizeFunctionOnNextCall(species_breakage);
  species_breakage();
  a.constructor = {};
  a.constructor[Symbol.species] = function() {};
  species_breakage();
  assertUnoptimized(species_breakage);
  assertEquals(24, result);
})();
