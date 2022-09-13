// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-gc --wasm-gc-js-interop --allow-natives-syntax

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

function MakeInstance() {
  let builder = new WasmModuleBuilder();
  let struct_type = builder.addStruct([makeField(kWasmI32, true)]);
  let array_type = builder.addArray(kWasmI32, true);
  builder.addFunction('MakeStruct', makeSig([], [kWasmExternRef]))
      .exportFunc()
      .addBody([
        kExprI32Const, 42,                       // --
        kGCPrefix, kExprStructNew, struct_type,  // --
        kGCPrefix, kExprExternExternalize        // --
      ]);
  builder.addFunction('MakeArray', makeSig([], [kWasmExternRef]))
      .exportFunc()
      .addBody([
        kExprI32Const, 2,                             // length
        kGCPrefix, kExprArrayNewDefault, array_type,  // --
        kGCPrefix, kExprExternExternalize             // --
      ]);

  return builder.instantiate();
}

let instance = MakeInstance();
const struct = instance.exports.MakeStruct();
const array = instance.exports.MakeArray();

function testThrowsRepeated(fn, ErrorType) {
  %PrepareFunctionForOptimization(fn);
  for (let i = 0; i < 5; i++) assertThrows(fn, ErrorType);
  %OptimizeFunctionOnNextCall(fn);
  assertThrows(fn, ErrorType);
  // TODO(7748): This assertion doesn't hold true, as some cases run into
  // deopt loops.
  // assertTrue(%ActiveTierIsTurbofan(fn));
}

function repeated(fn) {
  %PrepareFunctionForOptimization(fn);
  for (let i = 0; i < 5; i++) fn();
  %OptimizeFunctionOnNextCall(fn);
  fn();
  // TODO(7748): This assertion doesn't hold true, as some cases run into
  // deopt loops.
  // assertTrue(%ActiveTierIsTurbofan(fn));
}

// TODO: test repeated execution of functions using ICs, including optimized.
for (const wasm_obj of [struct, array]) {
  testThrowsRepeated(() => wasm_obj.foo, TypeError);
  testThrowsRepeated(() => { wasm_obj.foo = 42; }, TypeError);
  testThrowsRepeated(() => wasm_obj[0], TypeError);
  testThrowsRepeated(() => { wasm_obj[0] = undefined; }, TypeError);
  testThrowsRepeated(() => wasm_obj.__proto__, TypeError);
  testThrowsRepeated(() => Object.prototype.__proto__.call(wasm_obj), TypeError);
  testThrowsRepeated(() => wasm_obj.__proto__ = null, TypeError);
  testThrowsRepeated(() => JSON.stringify(wasm_obj), TypeError);
  testThrowsRepeated(() => { for (let p in wasm_obj) { } }, TypeError);
  testThrowsRepeated(() => { for (let p of wasm_obj) { } }, TypeError);
  testThrowsRepeated(() => wasm_obj.toString(), TypeError);
  testThrowsRepeated(() => wasm_obj.valueOf(), TypeError);
  testThrowsRepeated(() => "" + wasm_obj, TypeError);
  testThrowsRepeated(() => 0 + wasm_obj, TypeError);
  testThrowsRepeated(() => { delete wasm_obj.foo; }, TypeError);
  testThrowsRepeated(() => Object.freeze(wasm_obj), TypeError);
  testThrowsRepeated(() => Object.seal(wasm_obj), TypeError);
  testThrowsRepeated(
      () => Object.prototype.__lookupGetter__.call(wasm_obj, 'foo'), TypeError);
  testThrowsRepeated(
      () => Object.prototype.__lookupSetter__.call(wasm_obj, 'foo'), TypeError);
  testThrowsRepeated(
      () => Object.prototype.__defineGetter__.call(wasm_obj, 'foo', () => 42),
      TypeError);
  testThrowsRepeated(
      () => Object.prototype.__defineSetter__.call(wasm_obj, 'foo', () => {}),
      TypeError);
  testThrowsRepeated(
      () => Object.defineProperty(wasm_obj, 'foo', {value: 42}), TypeError);

  repeated(() => assertEquals([], Object.getOwnPropertyNames(wasm_obj)));
  repeated(() => assertEquals([], Object.getOwnPropertySymbols(wasm_obj)));
  repeated(() => assertEquals({}, Object.getOwnPropertyDescriptors(wasm_obj)));
  repeated(() => assertEquals([], Object.keys(wasm_obj)));
  repeated(() => assertEquals([], Object.entries(wasm_obj)));
  repeated(() => assertEquals(undefined, Object.getOwnPropertyDescriptor(wasm_obj, "foo")));
  repeated(() => assertEquals(false, "foo" in wasm_obj));
  repeated(() => assertEquals(false, Object.prototype.hasOwnProperty.call(wasm_obj, "foo")));
  repeated(() => assertEquals(true, Object.isSealed(wasm_obj)));
  repeated(() => assertEquals(true, Object.isFrozen(wasm_obj)));
  repeated(() => assertEquals(false, Object.isExtensible(wasm_obj)));
  repeated(() => assertEquals("object", typeof wasm_obj));
  repeated(() => assertEquals("[object Object]", Object.prototype.toString.call(wasm_obj)));

  {
    let js_obj = {};
    js_obj.foo = wasm_obj;
    repeated(() => assertSame(wasm_obj, js_obj.foo));
    js_obj[0] = wasm_obj;
    repeated(() => assertSame(wasm_obj, js_obj[0]));
  }

  repeated(() => assertEquals(42, wasm_obj ? 42 : 0));

  repeated(() => assertFalse(Array.isArray(wasm_obj)));
  testThrowsRepeated(() => wasm_obj(), TypeError);
  testThrowsRepeated(() => ++wasm_obj, TypeError);
  testThrowsRepeated(() => wasm_obj--, TypeError);

  testThrowsRepeated(() => [...wasm_obj], TypeError);
  repeated(() => assertEquals({}, {...wasm_obj}));
  repeated(() => ((...wasm_obj) => {})());
  repeated(() => assertSame(wasm_obj, ({wasm_obj}).wasm_obj));
  testThrowsRepeated(() => ({[wasm_obj]: null}), TypeError);
  testThrowsRepeated(() => `${wasm_obj}`, TypeError);
  testThrowsRepeated(() => wasm_obj`test`, TypeError);
  testThrowsRepeated(() => new wasm_obj, TypeError);
  testThrowsRepeated(() => wasm_obj?.property, TypeError);
  testThrowsRepeated(() => +wasm_obj, TypeError);
  testThrowsRepeated(() => -wasm_obj, TypeError);
  testThrowsRepeated(() => ~wasm_obj, TypeError);
  repeated(() => assertFalse(!wasm_obj));
  repeated(() => assertEquals(undefined, void wasm_obj));
  testThrowsRepeated(() => wasm_obj - 2, TypeError);
  testThrowsRepeated(() => wasm_obj * 2, TypeError);
  testThrowsRepeated(() => wasm_obj / 2, TypeError);
  testThrowsRepeated(() => wasm_obj ** 2, TypeError);
  testThrowsRepeated(() => wasm_obj << 2, TypeError);
  testThrowsRepeated(() => wasm_obj >> 2, TypeError);
  testThrowsRepeated(() => 2 >>> wasm_obj, TypeError);
  testThrowsRepeated(() => 2 % wasm_obj, TypeError);
  testThrowsRepeated(() => 2 == wasm_obj, TypeError);
  repeated(() => assertTrue(wasm_obj == wasm_obj));
  repeated(() => assertTrue(wasm_obj === wasm_obj));
  repeated(() => assertFalse(wasm_obj != wasm_obj));
  repeated(() => assertFalse(wasm_obj !== wasm_obj));
  repeated(() => assertFalse(struct == array));
  repeated(() => assertTrue(struct != array));
  testThrowsRepeated(() => wasm_obj < wasm_obj, TypeError);
  testThrowsRepeated(() => wasm_obj <= wasm_obj, TypeError);
  testThrowsRepeated(() => wasm_obj >= wasm_obj, TypeError);
  testThrowsRepeated(() => wasm_obj | 1, TypeError);
  testThrowsRepeated(() => 1 & wasm_obj, TypeError);
  testThrowsRepeated(() => wasm_obj ^ wasm_obj, TypeError);
  repeated(() => assertTrue(wasm_obj? true : false));
  testThrowsRepeated(() => wasm_obj += 1, TypeError);
  let tmp = 1;
  testThrowsRepeated(() => tmp += wasm_obj, TypeError);
  testThrowsRepeated(() => tmp <<= wasm_obj, TypeError);
  testThrowsRepeated(() => tmp &= wasm_obj, TypeError);
  testThrowsRepeated(() => tmp **= wasm_obj, TypeError);
  repeated(() => assertSame(wasm_obj, tmp &&= wasm_obj));
  tmp = 0;
  repeated(() => assertSame(wasm_obj, tmp ||= wasm_obj));
  tmp = null;
  repeated(() => assertSame(wasm_obj, tmp ??= wasm_obj));
  testThrowsRepeated(() => { let [] = wasm_obj; }, TypeError);
  testThrowsRepeated(() => { let [a, b] = wasm_obj; }, TypeError);
  testThrowsRepeated(() => { let [...all] = wasm_obj; }, TypeError);
  testThrowsRepeated(() => { let {a} = wasm_obj; }, TypeError);
  repeated(() => { let {} = wasm_obj; });
  repeated(() => {
    let {...rest} = wasm_obj;
    assertTrue(rest instanceof Object);
  });
  testThrowsRepeated(() => {with (wasm_obj) test;}, ReferenceError);
  repeated(() => {
    with (wasm_obj) var with_lookup = tmp;
    assertEquals(tmp, with_lookup);
  });
  repeated(() => {
    switch (wasm_obj) {
      case 0:
      default:
        throw 1;
      case wasm_obj:
        break;
    }
  });
  repeated(() => {
    try {
      throw wasm_obj;
    } catch(e) {
      assertEquals(e, wasm_obj);
    }
  });
  testThrowsRepeated(() => {class SubClass extends wasm_obj {}}, TypeError,
      "Class extends value [object Object] is not a constructor or null");
  repeated(() => {
    class TestMemberInit { x = wasm_obj; };
    assertSame(wasm_obj, new TestMemberInit().x);
  });
  repeated(() => assertSame(wasm_obj, eval("wasm_obj")));

  // Test functions of the global object.
  testThrowsRepeated(() => isFinite(wasm_obj), TypeError);
  testThrowsRepeated(() => isNaN(wasm_obj), TypeError);
  testThrowsRepeated(() => parseFloat(wasm_obj), TypeError);
  testThrowsRepeated(() => parseInt(wasm_obj), TypeError);
  testThrowsRepeated(() => decodeURI(wasm_obj), TypeError);
  testThrowsRepeated(() => decodeURIComponent(wasm_obj), TypeError);
  testThrowsRepeated(() => encodeURI(wasm_obj), TypeError);
  testThrowsRepeated(() => encodeURIComponent(wasm_obj), TypeError);
  // Test constructors of the global object as function.
  testThrowsRepeated(() => AggregateError(wasm_obj), TypeError);
  repeated(() => assertSame(wasm_obj, Array(wasm_obj)[0]));
  testThrowsRepeated(() => ArrayBuffer(wasm_obj), TypeError);
  testThrowsRepeated(() => BigInt(wasm_obj), TypeError);
  testThrowsRepeated(() => BigInt64Array(wasm_obj), TypeError);
  testThrowsRepeated(() => BigUint64Array(wasm_obj), TypeError);
  repeated(() => assertEquals(true, Boolean(wasm_obj)));
  testThrowsRepeated(() => DataView(wasm_obj), TypeError);
  repeated(() => {
    let date = Date(wasm_obj);
    assertEquals("string", typeof date);
  });
  testThrowsRepeated(() => Error(wasm_obj), TypeError);
  testThrowsRepeated(() => EvalError(wasm_obj), TypeError);
  testThrowsRepeated(() => EvalError(wasm_obj), TypeError);
  testThrowsRepeated(() => Float64Array(wasm_obj), TypeError);
  testThrowsRepeated(() => Function(wasm_obj), TypeError);
  testThrowsRepeated(() => Int8Array(wasm_obj), TypeError);
  testThrowsRepeated(() => Int16Array(wasm_obj), TypeError);
  testThrowsRepeated(() => Int32Array(wasm_obj), TypeError);
  testThrowsRepeated(() => Map(wasm_obj), TypeError);
  testThrowsRepeated(() => Number(wasm_obj), TypeError);
  repeated(() => assertSame(wasm_obj, Object(wasm_obj)));
  testThrowsRepeated(() => Promise(wasm_obj), TypeError);
  testThrowsRepeated(() => Proxy(wasm_obj), TypeError);
  testThrowsRepeated(() => RangeError(wasm_obj), TypeError);
  testThrowsRepeated(() => ReferenceError(wasm_obj), TypeError);
  testThrowsRepeated(() => RegExp(wasm_obj), TypeError);
  testThrowsRepeated(() => Set(wasm_obj), TypeError);
  testThrowsRepeated(() => SharedArrayBuffer(wasm_obj), TypeError);
  testThrowsRepeated(() => String(wasm_obj), TypeError);
  testThrowsRepeated(() => Symbol(wasm_obj), TypeError);
  testThrowsRepeated(() => SyntaxError(wasm_obj), TypeError);
  testThrowsRepeated(() => TypeError(wasm_obj), TypeError);
  testThrowsRepeated(() => Uint8Array(wasm_obj), TypeError);
  testThrowsRepeated(() => Uint16Array(wasm_obj), TypeError);
  testThrowsRepeated(() => Uint32Array(wasm_obj), TypeError);
  testThrowsRepeated(() => URIError(wasm_obj), TypeError);
  testThrowsRepeated(() => WeakMap(wasm_obj), TypeError);
  testThrowsRepeated(() => WeakRef(wasm_obj), TypeError);
  testThrowsRepeated(() => WeakSet(wasm_obj), TypeError);
  // Test constructors of the global object with new.
  testThrowsRepeated(() => new AggregateError(wasm_obj), TypeError);
  repeated(() => assertSame(wasm_obj, new Array(wasm_obj)[0]));

  testThrowsRepeated(() => new ArrayBuffer(wasm_obj), TypeError);
  testThrowsRepeated(() => new BigInt(wasm_obj), TypeError);
  testThrowsRepeated(() => new BigInt64Array(wasm_obj), TypeError);
  testThrowsRepeated(() => new BigUint64Array(wasm_obj), TypeError);
  repeated(() => assertEquals(true, (new Boolean(wasm_obj)).valueOf()));
  testThrowsRepeated(() => new DataView(wasm_obj), TypeError);
  testThrowsRepeated(() => new Date(wasm_obj), TypeError);
  testThrowsRepeated(() => new Error(wasm_obj), TypeError);
  testThrowsRepeated(() => new EvalError(wasm_obj), TypeError);
  testThrowsRepeated(() => new EvalError(wasm_obj), TypeError);
  testThrowsRepeated(() => new Float64Array(wasm_obj), TypeError);
  testThrowsRepeated(() => new Function(wasm_obj), TypeError);
  testThrowsRepeated(() => new Int8Array(wasm_obj), TypeError);
  testThrowsRepeated(() => new Int16Array(wasm_obj), TypeError);
  testThrowsRepeated(() => new Int32Array(wasm_obj), TypeError);
  testThrowsRepeated(() => new Map(wasm_obj), TypeError);
  testThrowsRepeated(() => new Number(wasm_obj), TypeError);
  repeated(() => assertSame(wasm_obj, new Object(wasm_obj)));
  testThrowsRepeated(() => new Promise(wasm_obj), TypeError);
  testThrowsRepeated(() => new Proxy(wasm_obj), TypeError);
  testThrowsRepeated(() => new RangeError(wasm_obj), TypeError);
  testThrowsRepeated(() => new ReferenceError(wasm_obj), TypeError);
  testThrowsRepeated(() => new RegExp(wasm_obj), TypeError);
  testThrowsRepeated(() => new Set(wasm_obj), TypeError);
  testThrowsRepeated(() => new SharedArrayBuffer(wasm_obj), TypeError);
  testThrowsRepeated(() => new String(wasm_obj), TypeError);
  testThrowsRepeated(() => new Symbol(wasm_obj), TypeError);
  testThrowsRepeated(() => new SyntaxError(wasm_obj), TypeError);
  testThrowsRepeated(() => new TypeError(wasm_obj), TypeError);
  testThrowsRepeated(() => new Uint8Array(wasm_obj), TypeError);
  testThrowsRepeated(() => new Uint16Array(wasm_obj), TypeError);
  testThrowsRepeated(() => new Uint32Array(wasm_obj), TypeError);
  testThrowsRepeated(() => new URIError(wasm_obj), TypeError);
  testThrowsRepeated(() => new WeakMap(wasm_obj), TypeError);
  repeated(() => assertSame(wasm_obj, new WeakRef(wasm_obj).deref()));
  testThrowsRepeated(() => new WeakSet(wasm_obj), TypeError);

  repeated(() => {
    let tgt = {};
    Object.assign(tgt, wasm_obj);
    assertEquals({}, tgt);
  });
  testThrowsRepeated(() => Object.create(wasm_obj), TypeError);
  testThrowsRepeated(() => ({}).__proto__ = wasm_obj, TypeError);
  testThrowsRepeated(
      () => Object.defineProperties(wasm_obj, {prop: {value: 1}}),
      TypeError);
  testThrowsRepeated(() => Object.defineProperty(wasm_obj, "prop", {value: 1}),
      TypeError);
  testThrowsRepeated(() => Object.fromEntries(wasm_obj), TypeError);
  testThrowsRepeated(() => Object.getPrototypeOf(wasm_obj), TypeError);
  repeated(() => assertFalse(Object.hasOwn(wasm_obj, "test")));
  testThrowsRepeated(() => Object.preventExtensions(wasm_obj), TypeError);
  testThrowsRepeated(() => Object.setPrototypeOf(wasm_obj, Object), TypeError);
  repeated(() => assertEquals([], Object.values(wasm_obj)));
  testThrowsRepeated(() => wasm_obj.toString(), TypeError);

  {
    let fct = function(x) { return [this, x] };
    repeated(() => assertEquals([wasm_obj, 1], fct.apply(wasm_obj, [1])));
    repeated(() => assertEquals([new Number(1), wasm_obj], fct.apply(1, [wasm_obj])));
    testThrowsRepeated(() => fct.apply(1, wasm_obj), TypeError);
    repeated(() => assertEquals([wasm_obj, 1], fct.bind(wasm_obj)(1)));
    repeated(() => assertEquals([wasm_obj, 1], fct.call(wasm_obj, 1)));
  }

  testThrowsRepeated(() => Symbol.for(wasm_obj), TypeError);
  testThrowsRepeated(() => Symbol.keyFor(wasm_obj), TypeError);
  repeated(() => assertFalse(Number.isFinite(wasm_obj)));
  repeated(() => assertFalse(Number.isInteger(wasm_obj)));
  repeated(() => assertFalse(Number.isNaN(wasm_obj)));
  repeated(() => assertFalse(Number.isSafeInteger(wasm_obj)));
  testThrowsRepeated(() => Number.parseFloat(wasm_obj), TypeError);
  testThrowsRepeated(() => Number.parseInt(wasm_obj), TypeError);
  testThrowsRepeated(() => BigInt.asIntN(2, wasm_obj), TypeError);
  testThrowsRepeated(
      () => BigInt.asUintN(wasm_obj, new BigInt(123)), TypeError);
  testThrowsRepeated(() => Math.abs(wasm_obj), TypeError);
  testThrowsRepeated(() => Math.acos(wasm_obj), TypeError);
  testThrowsRepeated(() => Math.acosh(wasm_obj), TypeError);
  testThrowsRepeated(() => Math.asin(wasm_obj), TypeError);
  testThrowsRepeated(() => Math.asinh(wasm_obj), TypeError);
  testThrowsRepeated(() => Math.atan(wasm_obj), TypeError);
  testThrowsRepeated(() => Math.atanh(wasm_obj), TypeError);
  testThrowsRepeated(() => Math.atan2(wasm_obj), TypeError);
  testThrowsRepeated(() => Math.cbrt(wasm_obj), TypeError);
  testThrowsRepeated(() => Math.ceil(wasm_obj), TypeError);
  testThrowsRepeated(() => Math.clz32(wasm_obj), TypeError);
  testThrowsRepeated(() => Math.cos(wasm_obj), TypeError);
  testThrowsRepeated(() => Math.cosh(wasm_obj), TypeError);
  testThrowsRepeated(() => Math.exp(wasm_obj), TypeError);
  testThrowsRepeated(() => Math.expm1(wasm_obj), TypeError);
  testThrowsRepeated(() => Math.floor(wasm_obj), TypeError);
  testThrowsRepeated(() => Math.fround(wasm_obj), TypeError);
  testThrowsRepeated(() => Math.hypot(wasm_obj), TypeError);
  testThrowsRepeated(() => Math.imul(wasm_obj, wasm_obj), TypeError);
  testThrowsRepeated(() => Math.log(wasm_obj), TypeError);
  testThrowsRepeated(() => Math.log1p(wasm_obj), TypeError);
  testThrowsRepeated(() => Math.log10(wasm_obj), TypeError);
  testThrowsRepeated(() => Math.log2(wasm_obj), TypeError);
  testThrowsRepeated(() => Math.max(2, wasm_obj), TypeError);
  testThrowsRepeated(() => Math.min(2, wasm_obj), TypeError);
  testThrowsRepeated(() => Math.pow(2, wasm_obj), TypeError);
  testThrowsRepeated(() => Math.pow(wasm_obj, 2), TypeError);
  testThrowsRepeated(() => Math.round(wasm_obj), TypeError);
  testThrowsRepeated(() => Math.sign(wasm_obj), TypeError);
  testThrowsRepeated(() => Math.sin(wasm_obj), TypeError);
  testThrowsRepeated(() => Math.sinh(wasm_obj), TypeError);
  testThrowsRepeated(() => Math.sqrt(wasm_obj), TypeError);
  testThrowsRepeated(() => Math.tan(wasm_obj), TypeError);
  testThrowsRepeated(() => Math.tanh(wasm_obj), TypeError);
  testThrowsRepeated(() => Math.trunc(wasm_obj), TypeError);
  testThrowsRepeated(() => Date.parse(wasm_obj), TypeError);
  testThrowsRepeated(() => Date.UTC(wasm_obj), TypeError);
  testThrowsRepeated(() => (new Date()).setDate(wasm_obj), TypeError);
  testThrowsRepeated(() => (new Date()).setFullYear(wasm_obj), TypeError);
  testThrowsRepeated(() => (new Date()).setHours(wasm_obj), TypeError);
  testThrowsRepeated(() => (new Date()).setMilliseconds(wasm_obj), TypeError);
  testThrowsRepeated(() => (new Date()).setMinutes(wasm_obj), TypeError);
  testThrowsRepeated(() => (new Date()).setMonth(wasm_obj), TypeError);
  testThrowsRepeated(() => (new Date()).setSeconds(wasm_obj), TypeError);
  testThrowsRepeated(() => (new Date()).setTime(wasm_obj), TypeError);
  testThrowsRepeated(() => (new Date()).setUTCDate(wasm_obj), TypeError);
  testThrowsRepeated(() => (new Date()).setUTCFullYear(wasm_obj), TypeError);
  testThrowsRepeated(() => (new Date()).setUTCHours(wasm_obj), TypeError);
  testThrowsRepeated(
      () => (new Date()).setUTCMilliseconds(wasm_obj), TypeError);
  testThrowsRepeated(() => (new Date()).setUTCMinutes(wasm_obj), TypeError);
  testThrowsRepeated(() => (new Date()).setUTCMonth(wasm_obj), TypeError);
  testThrowsRepeated(() => (new Date()).setUTCSeconds(wasm_obj), TypeError);
  // Date.prototype.toJSON() parameter `key` is ignored.
  repeated(() => (new Date()).toJSON(wasm_obj));
  testThrowsRepeated(() => String.fromCharCode(wasm_obj), TypeError);
  testThrowsRepeated(() => String.fromCodePoint(wasm_obj), TypeError);
  testThrowsRepeated(() => String.raw(wasm_obj), TypeError);
  testThrowsRepeated(() => "test".at(wasm_obj), TypeError);
  testThrowsRepeated(() => "test".charAt(wasm_obj), TypeError);
  testThrowsRepeated(() => "test".charCodeAt(wasm_obj), TypeError);
  testThrowsRepeated(() => "test".codePointAt(wasm_obj), TypeError);
  testThrowsRepeated(() => "test".concat(wasm_obj), TypeError);
  testThrowsRepeated(() => "test".endsWith(wasm_obj), TypeError);
  testThrowsRepeated(() => "test".endsWith("t", wasm_obj), TypeError);
  testThrowsRepeated(() => "test".includes(wasm_obj), TypeError);
  testThrowsRepeated(() => "test".includes("t", wasm_obj), TypeError);
  testThrowsRepeated(() => "test".indexOf(wasm_obj), TypeError);
  testThrowsRepeated(() => "test".lastIndexOf(wasm_obj), TypeError);
  testThrowsRepeated(() => "test".localeCompare(wasm_obj), TypeError);
  testThrowsRepeated(() => "test".match(wasm_obj), TypeError);
  testThrowsRepeated(() => "test".matchAll(wasm_obj), TypeError);
  testThrowsRepeated(() => "test".normalize(wasm_obj), TypeError);
  testThrowsRepeated(() => "test".padEnd(wasm_obj), TypeError);
  testThrowsRepeated(() => "test".padStart(10, wasm_obj), TypeError);
  testThrowsRepeated(() => "test".repeat(wasm_obj), TypeError);
  testThrowsRepeated(() => "test".replace(wasm_obj, ""), TypeError);
  testThrowsRepeated(() => "test".replace("t", wasm_obj), TypeError);
  testThrowsRepeated(() => "test".replaceAll(wasm_obj, ""), TypeError);
  testThrowsRepeated(() => "test".search(wasm_obj), TypeError);
  testThrowsRepeated(() => "test".slice(wasm_obj, 2), TypeError);
  testThrowsRepeated(() => "test".split(wasm_obj), TypeError);
  testThrowsRepeated(() => "test".startsWith(wasm_obj), TypeError);
  testThrowsRepeated(() => "test".substring(wasm_obj), TypeError);
  testThrowsRepeated(() => Array.from(wasm_obj), TypeError);
  repeated(() => assertFalse(Array.isArray(wasm_obj)));
  repeated(() => assertEquals([wasm_obj], Array.of(wasm_obj)));
  testThrowsRepeated(() => [1, 2].at(wasm_obj), TypeError);
  repeated(() => assertEquals([1, wasm_obj],[1].concat(wasm_obj)));
  testThrowsRepeated(() => [1, 2].copyWithin(wasm_obj), TypeError);
  testThrowsRepeated(() => [1, 2].every(wasm_obj), TypeError);
  repeated(() => assertEquals([1, wasm_obj, 3],[1, 2, 3].fill(wasm_obj, 1, 2)));
  testThrowsRepeated(() => [1, 2].filter(wasm_obj), TypeError);
  repeated(() => assertEquals([wasm_obj], [undefined, wasm_obj, null].filter(
      function(v) { return v == this; }, wasm_obj)));
  testThrowsRepeated(() => [1, 2].find(wasm_obj), TypeError);
  testThrowsRepeated(() => [1, 2].findIndex(wasm_obj), TypeError);
  testThrowsRepeated(() => [1, 2].findLast(wasm_obj), TypeError);
  testThrowsRepeated(() => [1, 2].findLastIndex(wasm_obj), TypeError);
  testThrowsRepeated(() => [1, 2].flat(wasm_obj), TypeError);
  testThrowsRepeated(() => [1, 2].flatMap(wasm_obj), TypeError);
  testThrowsRepeated(() => [1, 2].forEach(wasm_obj), TypeError);
  repeated(() => {
    let res = [];
    [1, 2].forEach(function(x) {res.push(this); }, wasm_obj);
    assertEquals([wasm_obj, wasm_obj], res);
  });
  repeated(() => assertTrue([wasm_obj].includes(wasm_obj)));
  repeated(() => assertFalse([1].includes(wasm_obj)));
  repeated(() => assertEquals(1, [0, wasm_obj, 2].indexOf(wasm_obj)));
  testThrowsRepeated(() => ["a", "b"].join(wasm_obj), TypeError);
  repeated(() => assertEquals(1, [0, wasm_obj, 2].lastIndexOf(wasm_obj)));
  testThrowsRepeated(() => [1, 2].map(wasm_obj), TypeError);
  repeated(() => assertEquals([wasm_obj, wasm_obj], [1, 2].map(
      function () { return this; }, wasm_obj)));
  repeated(() => {
    let arr = [1];
    arr.push(wasm_obj, 3);
    assertEquals([1, wasm_obj, 3], arr);
  });
  testThrowsRepeated(() => [1, 2].reduce(wasm_obj), TypeError);
  repeated(() => assertSame(wasm_obj, [].reduce(() => null, wasm_obj)));
  testThrowsRepeated(() => [1, 2].reduceRight(wasm_obj), TypeError);
  testThrowsRepeated(() => [1, 2].slice(wasm_obj, 2), TypeError);
  testThrowsRepeated(() => [1, 2].some(wasm_obj), TypeError);
  testThrowsRepeated(() => [1, 2].sort(wasm_obj), TypeError);
  testThrowsRepeated(() => [1, 2].splice(1, wasm_obj), TypeError);
  repeated(() => {
    let arr = [1, 2];
    arr.unshift(wasm_obj);
    assertEquals([wasm_obj, 1, 2], arr);
  });
  testThrowsRepeated(() => Int8Array.from(wasm_obj), TypeError);
  testThrowsRepeated(() => Int8Array.of(wasm_obj), TypeError);
  for (let ArrayType of [
    Int8Array, Int16Array, Int32Array, Uint8Array, Uint16Array, Uint32Array]) {
    let array = ArrayType.of(1, 2, 3);
    testThrowsRepeated(() => array.at(wasm_obj), TypeError);
    testThrowsRepeated(() => array.copyWithin(wasm_obj), TypeError);
    testThrowsRepeated(() => array.fill(wasm_obj, 0, 1), TypeError);
    testThrowsRepeated(() => array.filter(wasm_obj), TypeError);
    testThrowsRepeated(() => array.find(wasm_obj), TypeError);
    testThrowsRepeated(() => array.findIndex(wasm_obj), TypeError);
    testThrowsRepeated(() => array.findLast(wasm_obj), TypeError);
    testThrowsRepeated(() => array.findLastIndex(wasm_obj), TypeError);
    testThrowsRepeated(() => array.forEach(wasm_obj), TypeError);
    repeated(() => assertFalse(array.includes(wasm_obj)));
    repeated(() => assertEquals(-1, array.indexOf(wasm_obj)));
    testThrowsRepeated(() => array.join(wasm_obj), TypeError);
    repeated(() => assertEquals(-1, array.lastIndexOf(wasm_obj)));
    testThrowsRepeated(() => array.map(wasm_obj), TypeError);
    testThrowsRepeated(() => array.map(() => wasm_obj), TypeError);
    testThrowsRepeated(() => array.reduce(wasm_obj), TypeError);
    testThrowsRepeated(() => array.reduceRight(wasm_obj), TypeError);
    testThrowsRepeated(() => array.set(wasm_obj), TypeError);
    testThrowsRepeated(() => array.set([wasm_obj]), TypeError);
    testThrowsRepeated(() => array.slice(wasm_obj, 1), TypeError);
    testThrowsRepeated(() => array.some(wasm_obj), TypeError);
    testThrowsRepeated(() => array.sort(wasm_obj), TypeError);
    testThrowsRepeated(() => array.subarray(0, wasm_obj), TypeError);
  }
  for (let MapType of [Map, WeakMap]) {
    repeated(() => {
      let val = new String("a");
      let map = new MapType([[val, wasm_obj], [wasm_obj, val]]);
      assertSame(wasm_obj, map.get(val));
      assertEquals(val, map.get(wasm_obj));
      assertTrue(map.has(wasm_obj));
      map.delete(wasm_obj);
      assertFalse(map.has(wasm_obj));
      assertThrows(() => map.forEach(wasm_obj), TypeError);
      map.set(wasm_obj, wasm_obj);
      assertSame(wasm_obj, map.get(wasm_obj));
    });
  }
  for (let SetType of [Set, WeakSet]) {
    repeated(() => {
      let set = new SetType([new String("a"), wasm_obj]);
      set.add(wasm_obj);
      assertTrue(set.has(wasm_obj));
      set.delete(wasm_obj);
      assertFalse(set.has(wasm_obj));
    });
  }
  repeated(() => assertFalse(ArrayBuffer.isView(wasm_obj)));
  testThrowsRepeated(
      () => (new ArrayBuffer(32)).slice(wasm_obj, wasm_obj), TypeError);
  testThrowsRepeated(
      () => (new SharedArrayBuffer(32)).slice(wasm_obj, wasm_obj), TypeError);
  let arrayBuf = new ArrayBuffer(32);
  let dataView = new DataView(arrayBuf);
  testThrowsRepeated(() => dataView.getBigInt64(wasm_obj), TypeError);
  testThrowsRepeated(() => dataView.getBigUint64(wasm_obj), TypeError);
  testThrowsRepeated(() => dataView.getFloat32(wasm_obj), TypeError);
  testThrowsRepeated(() => dataView.getFloat64(wasm_obj), TypeError);
  testThrowsRepeated(() => dataView.getInt8(wasm_obj), TypeError);
  testThrowsRepeated(() => dataView.getInt16(wasm_obj), TypeError);
  testThrowsRepeated(() => dataView.getInt32(wasm_obj), TypeError);
  testThrowsRepeated(() => dataView.getUint8(wasm_obj), TypeError);
  testThrowsRepeated(() => dataView.getUint16(wasm_obj), TypeError);
  testThrowsRepeated(() => dataView.getUint32(wasm_obj), TypeError);
  testThrowsRepeated(() => dataView.setBigInt64(wasm_obj), TypeError);
  testThrowsRepeated(() => dataView.setBigUint64(wasm_obj), TypeError);
  testThrowsRepeated(() => dataView.setFloat32(wasm_obj), TypeError);
  testThrowsRepeated(() => dataView.setFloat64(0, wasm_obj), TypeError);
  testThrowsRepeated(() => dataView.setInt8(wasm_obj), TypeError);
  testThrowsRepeated(() => dataView.setInt16(0, wasm_obj), TypeError);
  testThrowsRepeated(() => dataView.setInt32(wasm_obj), TypeError);
  testThrowsRepeated(() => dataView.setUint8(0, wasm_obj), TypeError);
  testThrowsRepeated(() => dataView.setUint16(wasm_obj), TypeError);
  testThrowsRepeated(() => dataView.setUint32(0, wasm_obj), TypeError);

  let i8Array = new Int8Array(32);
  testThrowsRepeated(() => Atomics.add(wasm_obj, 1, 2), TypeError);
  testThrowsRepeated(() => Atomics.add(i8Array, wasm_obj, 2), TypeError);
  testThrowsRepeated(() => Atomics.add(i8Array, 1, wasm_obj), TypeError);
  testThrowsRepeated(() => Atomics.and(wasm_obj, 1, 2), TypeError);
  testThrowsRepeated(() => Atomics.and(i8Array, wasm_obj, 2), TypeError);
  testThrowsRepeated(() => Atomics.and(i8Array, 1, wasm_obj), TypeError);
  testThrowsRepeated(() => Atomics.compareExchange(wasm_obj, 1, 2, 3), TypeError);
  testThrowsRepeated(() => Atomics.compareExchange(i8Array, wasm_obj, 2, 3), TypeError);
  testThrowsRepeated(() => Atomics.compareExchange(i8Array, 1, wasm_obj, 3), TypeError);
  testThrowsRepeated(() => Atomics.compareExchange(i8Array, 1, 2, wasm_obj), TypeError);
  testThrowsRepeated(() => Atomics.exchange(wasm_obj, 1, 2), TypeError);
  testThrowsRepeated(() => Atomics.exchange(i8Array, wasm_obj, 2), TypeError);
  testThrowsRepeated(() => Atomics.exchange(i8Array, 1, wasm_obj), TypeError);
  testThrowsRepeated(() => Atomics.isLockFree(wasm_obj), TypeError);
  testThrowsRepeated(() => Atomics.load(wasm_obj, 1), TypeError);
  testThrowsRepeated(() => Atomics.load(i8Array, wasm_obj), TypeError);
  testThrowsRepeated(() => Atomics.or(wasm_obj, 1, 2), TypeError);
  testThrowsRepeated(() => Atomics.or(i8Array, wasm_obj, 2), TypeError);
  testThrowsRepeated(() => Atomics.or(i8Array, 1, wasm_obj), TypeError);
  testThrowsRepeated(() => Atomics.store(wasm_obj, 1, 2), TypeError);
  testThrowsRepeated(() => Atomics.store(i8Array, wasm_obj, 2), TypeError);
  testThrowsRepeated(() => Atomics.store(i8Array, 1, wasm_obj), TypeError);
  testThrowsRepeated(() => Atomics.sub(wasm_obj, 1, 2), TypeError);
  testThrowsRepeated(() => Atomics.sub(i8Array, wasm_obj, 2), TypeError);
  testThrowsRepeated(() => Atomics.sub(i8Array, 1, wasm_obj), TypeError);
  testThrowsRepeated(() => Atomics.wait(wasm_obj, 1, 2, 3), TypeError);
  testThrowsRepeated(() => Atomics.wait(i8Array, wasm_obj, 2, 3), TypeError);
  testThrowsRepeated(() => Atomics.wait(i8Array, 1, wasm_obj, 3), TypeError);
  testThrowsRepeated(() => Atomics.wait(i8Array, 1, 2, wasm_obj), TypeError);
  testThrowsRepeated(() => Atomics.notify(wasm_obj, 1, 2), TypeError);
  testThrowsRepeated(() => Atomics.notify(i8Array, wasm_obj, 2), TypeError);
  testThrowsRepeated(() => Atomics.notify(i8Array, 1, wasm_obj), TypeError);
  testThrowsRepeated(() => Atomics.xor(wasm_obj, 1, 2), TypeError);
  testThrowsRepeated(() => Atomics.xor(i8Array, wasm_obj, 2), TypeError);
  testThrowsRepeated(() => Atomics.xor(i8Array, 1, wasm_obj), TypeError);

  testThrowsRepeated(() => JSON.parse(wasm_obj), TypeError);
  repeated(() => assertEquals({x: 1}, JSON.parse('{"x": 1}', wasm_obj)));
  testThrowsRepeated(() => JSON.stringify(wasm_obj), TypeError);
  repeated(() => assertEquals('{"x":1}', JSON.stringify({x: 1}, wasm_obj)));
  repeated(() => assertEquals('{"x":1}', JSON.stringify({x: 1}, null, wasm_obj)));

  repeated(() => assertThrowsAsync(Promise.all(wasm_obj), TypeError));
  repeated(() => assertThrowsAsync(Promise.all([wasm_obj]), TypeError));
  repeated(() => assertThrowsAsync(Promise.allSettled(wasm_obj), TypeError));
  repeated(() => Promise.allSettled([wasm_obj])
                 .then((info) => assertEquals("rejected", info[0].status)));
  repeated(() => assertThrowsAsync(Promise.any(wasm_obj), TypeError));
  repeated(() => assertThrowsAsync(Promise.any([wasm_obj]), AggregateError));
  repeated(() => assertThrowsAsync(Promise.race(wasm_obj), TypeError));
  repeated(() => assertThrowsAsync(Promise.race([wasm_obj]), TypeError));
  // TODO(7748): Allow WebAssembly objects as Promise resolve / reject arg.
  repeated(() => assertThrowsAsync(new Promise((resolve, reject) => resolve(wasm_obj)), TypeError));
  // TODO(7748): The assertThrowsAsync doesn't work correctly with rejected
  // promise and expectated error type.
  repeated(() => assertThrowsAsync(new Promise((resolve, reject) => reject(wasm_obj))));
  // If the `then` argument isn't a callback, it will simply be replaced with
  // an identity function (x) => x.
  repeated(() =>
      (new Promise((resolve) => resolve({})))
      .then(wasm_obj) // The value itself doesn't have any impact.
      .then((v) => assertEquals({}, v),
            () => assertTrue(false)));
  // If the `catch` argument isn't a callback, it will be replaced with a
  // thrower function (x) => { throw x; }.
  repeated(() =>
      (new Promise((resolve, reject) => reject({}))).then(() => null)
      .catch(wasm_obj) // The value itself doesn't have any impact.
      .then(() => assertTrue(false),
            (v) => assertEquals({}, v)));
  // `finally(wasm_obj)` behaves just like `then(wasm_obj, wasm_obj)`
  repeated(() =>
      (new Promise((resolve, reject) => resolve({})))
      .finally(wasm_obj)
      .then((v) => assertEquals({}, v),
            () => assertTrue(false)));
  repeated(() =>
      (new Promise((resolve, reject) => reject({})))
      .finally(wasm_obj)
      .then(() => assertTrue(false),
            (v) => assertEquals({}, v)));

  // TODO(7748): Using a wasm_obj as a resolved value in a then() chain
  // leads to a Type error.
  repeated(() =>
      (new Promise((resolve) => resolve({})))
      .then(() => wasm_obj)
      .then(() => assertTrue(false),
            (v) => assertTrue(v instanceof TypeError)));

  // Yielding wasm objects from a generator function is valid.
  repeated(() => {
    let gen = (function* () { yield wasm_obj; })();
    assertSame(wasm_obj, gen.next().value);
  });
  // Test passing wasm objects via next() back to a generator function.
  repeated(() => {
    let gen = (function* () { assertSame(wasm_obj, yield 1); })();
    assertEquals(1, gen.next().value);
    assertTrue(gen.next(wasm_obj).done); // triggers the assertEquals.
  });
  // Test passing wasm objects via return() to a generator function.
  repeated(() => {
    let gen = (function* () { yield 1; assertTrue(false); })();
    assertEquals({value: wasm_obj, done: true}, gen.return(wasm_obj));
  });
  // Test passing wasm objects via throw() to a generator function.
  repeated(() => {
    let gen = (function* () {
      try {
        yield 1;
        assertTrue(false); // unreached
      } catch (e) {
        assertSame(wasm_obj, e);
        return 2;
      }
    })();
    assertEquals(1, gen.next().value);
    gen.throw(wasm_obj); // wasm_obj is caught inside the generator
  });
  // Treating wasm objects as generators is invalid.
  repeated(() => {
    let gen = function* () { yield *wasm_obj; }
    assertThrows(() => gen().next(), TypeError);
  });

  // FIXME(mliedtke): Should we repeat the same testing for async generator
  // functions as for the synchronous ones?

  {
    let fct = function(x) { return [this, x] };
    repeated(() => assertEquals([wasm_obj, 1], Reflect.apply(fct, wasm_obj, [1])));
    repeated(() => assertEquals([{}, wasm_obj], Reflect.apply(fct, {}, [wasm_obj])));
    testThrowsRepeated(() => Reflect.apply(fct, 1, wasm_obj), TypeError);
    testThrowsRepeated(() => Reflect.apply(wasm_obj, null, []), TypeError);
  }
  testThrowsRepeated(() => Reflect.construct(wasm_obj, []), TypeError);
  testThrowsRepeated(() => Reflect.construct(Object, wasm_obj), TypeError);
  testThrowsRepeated(() => Reflect.construct(Object, [], wasm_obj), TypeError);
  testThrowsRepeated(
      () => Reflect.defineProperty(wasm_obj, "prop", {value: 1}),
      TypeError);
  testThrowsRepeated(
      () => Reflect.defineProperty({}, wasm_obj, {value: 1}),
      TypeError);
  {
    let obj = {};
    // The behavior of returning true and not throwing a type error is
    // consistent with passing {}.
    // FIXME(mliedtke): Should this throw?
    repeated(() => assertTrue(Reflect.defineProperty(obj, "prop", wasm_obj)));
    repeated(() => assertTrue(obj.hasOwnProperty("prop")));
    repeated(() => assertEquals(undefined, obj.prop));
    repeated(() => assertTrue(Reflect.defineProperty(obj, "prop2", {value: wasm_obj})));
    repeated(() => assertSame(wasm_obj, obj.prop2));
  }
  testThrowsRepeated(() => Reflect.deleteProperty(wasm_obj, "prop"),
                 TypeError);
  testThrowsRepeated(() => Reflect.deleteProperty({}, wasm_obj),
                TypeError);
  testThrowsRepeated(() => Reflect.get(wasm_obj, "prop"),
                TypeError);
  testThrowsRepeated(() => Reflect.getPrototypeOf(wasm_obj), TypeError);
  assertFalse(Reflect.has(wasm_obj, "prop"));
  repeated(() => assertTrue(Reflect.has({wasm_obj}, "wasm_obj")));

  repeated(() => assertFalse(Reflect.isExtensible(wasm_obj)));
  repeated(() => assertEquals([], Reflect.ownKeys(wasm_obj)));
  testThrowsRepeated(() => Reflect.preventExtensions(wasm_obj), TypeError);
  testThrowsRepeated(() => Reflect.set(wasm_obj, "prop", 123), TypeError);
  testThrowsRepeated(() => Reflect.setPrototypeOf(wasm_obj, Object.prototype), TypeError);

  {
    const handler = {
      get(target, prop, receiver) {
        return "proxied";
      }
    };
    let proxy = new Proxy(wasm_obj, handler);
    repeated(() => assertEquals("proxied", proxy.abc));
    testThrowsRepeated(() => proxy.abc = 123, TypeError);
  }
  {
    // FIXME(mliedtke): Using a wasm object as a handler should
    // probably throw.
    let proxy = new Proxy({}, wasm_obj);
    testThrowsRepeated(() => proxy.abc, TypeError);
  }
  {
    const handler = {
      get(target, prop, receiver) {
        return "proxied";
      }
    };
    let {proxy, revoke} = Proxy.revocable(wasm_obj, handler);
    repeated(() => assertEquals("proxied", proxy.abc));
    testThrowsRepeated(() => proxy.abc = 123, TypeError);
    revoke();
    testThrowsRepeated(() => proxy.abc, TypeError);
  }{
    // FIXME(mliedtke): Using a wasm object as a handler should
    // probably throw.
    let proxy = Proxy.revocable({}, wasm_obj).proxy;
    testThrowsRepeated(() => proxy.abc, TypeError);
  }


  // Ensure no statement re-asssigned wasm_obj by accident.
  repeated(() => assertTrue(wasm_obj == struct || wasm_obj == array));
}

repeated(async function testAsync() {
  for (let wasm_obj of [struct, array]) {
    try {
      await wasm_obj;
      throw "unreachable";
    } catch (e) {
      assertTrue(e instanceof TypeError);
    }
  }
});

// TODO(mliedtke): What about `export {struct, array}`?
// d8 doesn't seem to accept exports (as the file isn't a module?)
