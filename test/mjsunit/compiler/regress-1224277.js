// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --stress-concurrent-inlining

function nop() {
  return false;
}

function MjsUnitAssertionError(message) {
  this.message = message;
  let prevPrepareStackTrace = Error.prepareStackTrace;

  try {
    Error.prepareStackTrace = MjsUnitAssertionError.prepareStackTrace;
    this.stack = new Error("MjsUnitA****tionError").stack;
  } finally {
    Error.prepareStackTrace = prevPrepareStackTrace;
  }
}

MjsUnitAssertionError.prototype.toString = function () {
  return this.message + "\n\nStack: " + this.stack;
};

var assertSame;
var assertNotSame;
var assertEquals;
var deepEquals;
var assertNotEquals;
var assertEqualsDelta;
var assertArrayEquals;
var assertPropertiesEqual;
var assertToStringEquals;
var assertTrue;
var assertFalse;
var assertNull;
var assertNotNull;
var assertThrows;
var assertThrowsEquals;
var assertThrowsAsync;
var assertDoesNotThrow;
var assertInstanceof;
var assertUnreachable;
var assertContains;
var assertMatches;
var assertPromiseResult;
var promiseTestChain;
var promiseTestCount = 0;
var V8OptimizationStatus = {
  kIsFunction: 1 << 0,
  kNeverOptimize: 1 << 1,
  kAlwaysOptimize: 1 << 2,
  kMaybeDeopted: 1 << 3,
  kOptimized: 1 << 4,
  kTurboFanned: 1 << 5,
  kInterpreted: 1 << 6,
  kMarkedForOptimization: 1 << 7,
  kMarkedForConcurrentOptimization: 1 << 8,
  kOptimizingConcurrently: 1 << 9,
  kIsExecuting: 1 << 10,
  kTopmostFrameIsTurboFanned: 1 << 11,
  kLiteMode: 1 << 12,
  kMarkedForDeoptimization: 1 << 13,
  kBaseline: 1 << 14,
  kTopmostFrameIsInterpreted: 1 << 15,
  kTopmostFrameIsBaseline: 1 << 16
};
var failWithMessage;
var formatFailureText;
var prettyPrinted;

(function () {
  var ObjectPrototypeToString = Object.prototype.toString;
  var NumberPrototypeValueOf = Number.prototype.valueOf;
  var BooleanPrototypeValueOf = Boolean.prototype.valueOf;
  var StringPrototypeValueOf = String.prototype.valueOf;
  var DatePrototypeValueOf = Date.prototype.valueOf;
  var RegExpPrototypeToString = RegExp.prototype.toString;
  var ArrayPrototypeForEach = Array.prototype.forEach;
  var ArrayPrototypeJoin = Array.prototype.join;
  var ArrayPrototypeMap = Array.prototype.map;
  var ArrayPrototypePush = Array.prototype.push;
  var JSONStringify = JSON.stringify;
  var BigIntPrototypeValueOf;

  try {
    BigIntPrototypeValueOf = BigInt.prototype.valueOf;
  } catch (e) {}

  function classOf(object) {
    var string = ObjectPrototypeToString.call(object);
    return string.substring(8, string.length - 1);
  }

  function ValueOf(value) {
    switch (classOf(value)) {
      case "Number":
        return NumberPrototypeValueOf.call(value);

      case "BigInt":
        return BigIntPrototypeValueOf.call(value);

      case "String":
        return StringPrototypeValueOf.call(value);

      case "Boolean":
        return BooleanPrototypeValueOf.call(value);

      case "Date":
        return DatePrototypeValueOf.call(value);

      default:
        return value;
    }
  }

  prettyPrinted = function prettyPrinted(value) {
    switch (typeof value) {
      case "string":
        return JSONStringify(value);

      case "bigint":
        return String(value) + "n";

      case "number":
        if (value === 0 && 1 / value < 0) return "-0";

      case "boolean":
      case "undefined":
      case "function":
      case "symbol":
        return String(value);

      case "object":
        if (value === null) return "null";
        var objectClass = classOf(value);

        switch (objectClass) {
          case "Number":
          case "BigInt":
          case "String":
          case "Boolean":
          case "Date":
            return objectClass + "(" + prettyPrinted(ValueOf(value)) + ")";

          case "RegExp":
            return RegExpPrototypeToString.call(value);

          case "Array":
            var mapped = ArrayPrototypeMap.call(value, prettyPrintedArrayElement);
            var joined = ArrayPrototypeJoin.call(mapped, ",");
            return "[" + joined + "]";

          case "Uint8Array":
          case "Int8Array":
          case "Int16Array":
          case "Uint16Array":
          case "Uint32Array":
          case "Int32Array":
          case "Float32Array":
          case "Float64Array":
            var joined = ArrayPrototypeJoin.call(value, ",");
            return objectClass + "([" + joined + "])";

          case "Object":
            break;

          default:
            return objectClass + "(" + String(value) + ")";
        }

        var name = value.constructor.name;
        if (name) return name + "()";
        return "Object()";

      default:
        return "-- unknown value --";
    }
  };

  function prettyPrintedArrayElement(value, index, array) {
    if (value === undefined && !(index in array)) return "";
    return prettyPrinted(value);
  }

  failWithMessage = function failWithMessage(message) {
    throw new MjsUnitAssertionError(message);
  };

  formatFailureText = function (expectedText, found, name_opt) {
    var message = "Fail" + "ure";

    if (name_opt) {
      message += " (" + name_opt + ")";
    }

    var foundText = prettyPrinted(found);

    if (expectedText.length <= 40 && foundText.length <= 40) {
      message += ": expected <" + expectedText + "> found <" + foundText + ">";
    } else {
      message += ":\nexpected:\n" + expectedText + "\nfound:\n" + foundText;
    }

    return message;
  };

  function fail(expectedText, found, name_opt) {
    return failWithMessage(formatFailureText(expectedText, found, name_opt));
  }

  function deepObjectEquals(a, b) {
    var aProps = Object.keys(a);
    aProps.sort();
    var bProps = Object.keys(b);
    bProps.sort();

    if (!deepEquals(aProps, bProps)) {
      return false;
    }

    for (var i = 0; i < aProps.length; i++) {
      if (!deepEquals(a[aProps[i]], b[aProps[i]])) {
        return false;
      }
    }

    return true;
  }

  deepEquals = function deepEquals(a, b) {
    if (a === b) {
      if (a === 0) return 1 / a === 1 / b;
      return true;
    }

    if (typeof a !== typeof b) return false;
    if (typeof a === "number") return isNaN(a) && isNaN(b);
    if (typeof a !== "object" && typeof a !== "function") return false;
    var objectClass = classOf(a);
    if (objectClass !== classOf(b)) return false;

    if (objectClass === "RegExp") {
      return RegExpPrototypeToString.call(a) === RegExpPrototypeToString.call(b);
    }

    if (objectClass === "Function") return false;

    if (objectClass === "Array") {
      var elementCount = 0;

      if (a.length !== b.length) {
        return false;
      }

      for (var i = 0; i < a.length; i++) {
        if (!deepEquals(a[i], b[i])) return false;
      }

      return true;
    }

    if (objectClass === "String" || objectClass === "Number" || objectClass === "BigInt" || objectClass === "Boolean" || objectClass === "Date") {
      if (ValueOf(a) !== ValueOf(b)) return false;
    }

    return deepObjectEquals(a, b);
  };

  assertSame = function assertSame(expected, found, name_opt) {
    if (Object.is(expected, found)) return;
    fail(prettyPrinted(expected), found, name_opt);
  };

  assertNotSame = function assertNotSame(expected, found, name_opt) {
    if (!Object.is(expected, found)) return;
    fail("not same as " + prettyPrinted(expected), found, name_opt);
  };

  assertEquals = function assertEquals(expected, found, name_opt) {
    if (!deepEquals(found, expected)) {
      fail(prettyPrinted(expected), found, name_opt);
    }
  };

  assertNotEquals = function assertNotEquals(expected, found, name_opt) {
    if (deepEquals(found, expected)) {
      fail("not equals to " + prettyPrinted(expected), found, name_opt);
    }
  };

  assertEqualsDelta = function assertEqualsDelta(expected, found, delta, name_opt) {
    if (Math.abs(expected - found) > delta) {
      fail(prettyPrinted(expected) + " +- " + prettyPrinted(delta), found, name_opt);
    }
  };

  assertArrayEquals = function assertArrayEquals(expected, found, name_opt) {
    var start = "";

    if (name_opt) {
      start = name_opt + " - ";
    }

    assertEquals(expected.length, found.length, start + "array length");

    if (expected.length === found.length) {
      for (var i = 0; i < expected.length; ++i) {
        assertEquals(expected[i], found[i], start + "array element at index " + i);
      }
    }
  };

  assertPropertiesEqual = function assertPropertiesEqual(expected, found, name_opt) {
    if (!deepObjectEquals(expected, found)) {
      fail(expected, found, name_opt);
    }
  };

  assertToStringEquals = function assertToStringEquals(expected, found, name_opt) {
    if (expected !== String(found)) {
      fail(expected, found, name_opt);
    }
  };

  assertTrue = function assertTrue(value, name_opt) {
    assertEquals(true, value, name_opt);
  };

  assertFalse = function assertFalse(value, name_opt) {
    assertEquals(false, value, name_opt);
  };

  assertNull = function assertNull(value, name_opt) {
    if (value !== null) {
      fail("null", value, name_opt);
    }
  };

  assertNotNull = function assertNotNull(value, name_opt) {
    if (value === null) {
      fail("not null", value, name_opt);
    }
  };

  function executeCode(code) {
    if (typeof code === 'function') return code();
    if (typeof code === 'string') return eval(code);
    failWithMessage('Given code is neither function nor string, but ' + typeof code + ': <' + prettyPrinted(code) + '>');
  }

  function checkException(e, type_opt, cause_opt) {
    if (type_opt !== undefined) {
      assertEquals('function', typeof type_opt);
      assertInstanceof(e, type_opt);
    }

    if (RegExp !== undefined && cause_opt instanceof RegExp) {
      assertMatches(cause_opt, e.message, 'Error message');
    } else if (cause_opt !== undefined) {
      assertEquals(cause_opt, e.message, 'Error message');
    }
  }

  assertThrows = function assertThrows(code, type_opt, cause_opt) {
    if (arguments.length > 1 && type_opt === undefined) {
      failWithMessage('invalid use of assertThrows, unknown type_opt given');
    }

    if (type_opt !== undefined && typeof type_opt !== 'function') {
      failWithMessage('invalid use of assertThrows, maybe you want assertThrowsEquals');
    }

    try {
      executeCode(code);
    } catch (e) {
      checkException(e, type_opt, cause_opt);
      return;
    }

    let msg = 'Did not throw exception';
    if (type_opt !== undefined && type_opt.name !== undefined) msg += ', expected ' + type_opt.name;
    failWithMessage(msg);
  };

  assertThrowsEquals = function assertThrowsEquals(fun, val) {
    try {
      fun();
    } catch (e) {
      assertSame(val, e);
      return;
    }

    failWithMessage('Did not throw exception, expected ' + prettyPrinted(val));
  };

  assertInstanceof = function assertInstanceof(obj, type) {
    if (!(obj instanceof type)) {
      var actualTypeName = null;
      var actualConstructor = obj && Object.getPrototypeOf(obj).constructor;

      if (typeof actualConstructor === 'function') {
        actualTypeName = actualConstructor.name || String(actualConstructor);
      }

      failWithMessage('Object <' + prettyPrinted(obj) + '> is not an instance of <' + (type.name || type) + '>' + (actualTypeName ? ' but of <' + actualTypeName + '>' : ''));
    }
  };

  assertDoesNotThrow = function assertDoesNotThrow(code, name_opt) {
    try {
      executeCode(code);
    } catch (e) {
      if (e instanceof MjsUnitAssertionError) throw e;
      failWithMessage("threw an exception: " + (e.message || e));
    }
  };

  assertUnreachable = function assertUnreachable(name_opt) {
    var message = "Fail" + "ure: unreachable";

    if (name_opt) {
      message += " - " + name_opt;
    }

    failWithMessage(message);
  };

  assertContains = function (sub, value, name_opt) {
    if (value == null ? sub != null : value.indexOf(sub) == -1) {
      fail("contains '" + String(sub) + "'", value, name_opt);
    }
  };

  assertMatches = function (regexp, str, name_opt) {
    if (!(regexp instanceof RegExp)) {
      regexp = new RegExp(regexp);
    }

    if (!str.match(regexp)) {
      fail("should match '" + regexp + "'", str, name_opt);
    }
  };

  function concatenateErrors(stack, exception) {
    if (!exception.stack) exception = new Error(exception);

    if (typeof exception.stack !== 'string') {
      return exception;
    }

    exception.stack = stack + '\n\n' + exception.stack;
    return exception;
  }
})();


function description(msg) {}

function debug(msg) {}

function shouldBe(_a) {
  print(typeof _a == "function" ? _a() : eval(_a));
}

function shouldBeTrue(_a) {
  shouldBe(_a);
}

function shouldBeFalse(_a) {
  shouldBe(_a);
}

function shouldBeNaN(_a) {
  shouldBe(_a);
}

function shouldBeNull(_a) {
  shouldBe(_a);
}

function shouldNotThrow(_a) {
  shouldBe(_a);
}

function shouldThrow(_a) {
  shouldBe(_a);
}

function __isPropertyOfType(obj, name, type) {
  let desc;

  try {
    desc = Object.getOwnPropertyDescriptor(obj, name);
  } catch (e) {
    return false;
  }

  if (!desc) return false;
  return typeof type === 'undefined' || typeof desc.value === type;
}

function __getProperties(obj, type) {
  if (typeof obj === "undefined" || obj === null) return [];
  let properties = [];

  for (let name of Object.getOwnPropertyNames(obj)) {
    if (__isPropertyOfType(obj, name, type)) properties.push(name);
  }

  let proto = Object.getPrototypeOf(obj);

  while (proto && proto != Object.prototype) {
    Object.getOwnPropertyNames(proto).forEach(name => {
      if (name !== 'constructor') {
        if (__isPropertyOfType(proto, name, type)) properties.push(name);
      }
    });
    proto = Object.getPrototypeOf(proto);
  }

  return properties;
}

function __getRandomProperty(obj, seed) {
  let properties = __getProperties(obj);

  if (!properties.length) return undefined;
  return properties[seed % properties.length];
}

function __callRandomFunction(obj, seed, ...args) {
  let functions = __getProperties(obj, 'function');

  if (!functions.length) return;
  let random_function = functions[seed % functions.length];

  try {
    obj[random_function](...args);
  } catch (e) {}
}

let __callGC;

(function () {
  let countGC = 0;

  __callGC = function () {
    if (countGC++ < 50) {
      gc();
    }
  };
})();

try {
  this.failWithMessage = nop;
} catch (e) {}

try {
  this.triggerAssertFalse = nop;
} catch (e) {}

try {
  this.quit = nop;
} catch (e) {}

const __v_0 = (__v_1, __v_2) => __v_1 + __v_2;

try {
  for (__v_3 in [0, 0]) {}
} catch (e) {}

try {
  __callRandomFunction(__v_0, 851918, 9007199254740991);
} catch (e) {}

try {
  (function () {
    function __f_6(__v_12) {
      return (
        __v_0 >>> 24 & 0xffff
      );
    }

    try {
      %PrepareFunctionForOptimization(__f_6);
    } catch (e) {}

    try {
      __f_6(
      -3);
    } catch (e) {}

    try {
      %OptimizeFunctionOnNextCall(__f_6);
    } catch (e) {}

    try {
      assertEquals(0, __f_6(1));
    } catch (e) {}

    try {
      assertEquals(100, __f_6((
      103 << 24) + 42));
    } catch (e) {}

  })();
} catch (e) {}

var __v_13 = {};

function __f_7() {
  return !__v_13.bar++;
}

%PrepareFunctionForOptimization(__f_7);
assertFalse(__f_7());
assertEquals(-1, __v_13.bar);
%OptimizeFunctionOnNextCall(__f_7);
assertFalse(__f_7());
assertEquals(0, __v_13.bar);
assertTrue(__f_7());
assertEquals(1, __v_13.bar);
assertFalse(__f_7());
