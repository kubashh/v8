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

function testThrowsRepeated(fn) {
  %PrepareFunctionForOptimization(fn);
  for (let i = 0; i < 5; i++) assertThrows(fn, TypeError);
  %OptimizeFunctionOnNextCall(fn);
  assertThrows(fn, TypeError);
}

// TODO: test repeated execution of functions using ICs, including optimized.
for (const wasm_obj of [struct, array]) {
  testThrowsRepeated(() => wasm_obj.foo);
  testThrowsRepeated(() => { wasm_obj.foo = 42; });
  testThrowsRepeated(() => wasm_obj[0]);
  testThrowsRepeated(() => { wasm_obj[0] = undefined; });
  assertThrows(() => wasm_obj.__proto__, TypeError);
  assertThrows(() => Object.prototype.__proto__.call(wasm_obj), TypeError);
  assertThrows(() => wasm_obj.__proto__ = null, TypeError);
  assertThrows(() => JSON.stringify(wasm_obj), TypeError);
  assertThrows(() => { for (let p in wasm_obj) { } }, TypeError);
  assertThrows(() => { for (let p of wasm_obj) { } }, TypeError);
  assertThrows(() => wasm_obj.toString(), TypeError);
  assertThrows(() => wasm_obj.valueOf(), TypeError);
  assertThrows(() => "" + wasm_obj, TypeError);
  assertThrows(() => 0 + wasm_obj, TypeError);
  assertThrows(() => { delete wasm_obj.foo; }, TypeError);
  assertThrows(() => Object.freeze(wasm_obj), TypeError);
  assertThrows(() => Object.seal(wasm_obj), TypeError);
  assertThrows(
      () => Object.prototype.__lookupGetter__.call(wasm_obj, 'foo'), TypeError);
  assertThrows(
      () => Object.prototype.__lookupSetter__.call(wasm_obj, 'foo'), TypeError);
  assertThrows(
      () => Object.prototype.__defineGetter__.call(wasm_obj, 'foo', () => 42),
      TypeError);
  assertThrows(
      () => Object.prototype.__defineSetter__.call(wasm_obj, 'foo', () => {}),
      TypeError);
  assertThrows(
      () => Object.defineProperty(wasm_obj, 'foo', {value: 42}), TypeError);

  assertEquals([], Object.getOwnPropertyNames(wasm_obj));
  assertEquals([], Object.getOwnPropertySymbols(wasm_obj));
  assertEquals({}, Object.getOwnPropertyDescriptors(wasm_obj));
  assertEquals([], Object.keys(wasm_obj));
  assertEquals([], Object.entries(wasm_obj));
  assertEquals(undefined, Object.getOwnPropertyDescriptor(wasm_obj, "foo"));
  assertEquals(false, "foo" in wasm_obj);
  assertEquals(false, Object.prototype.hasOwnProperty.call(wasm_obj, "foo"));
  assertEquals(true, Object.isSealed(wasm_obj));
  assertEquals(true, Object.isFrozen(wasm_obj));
  assertEquals(false, Object.isExtensible(wasm_obj));
  assertEquals("object", typeof wasm_obj);
  assertEquals("[object Object]", Object.prototype.toString.call(wasm_obj));

  {
    let js_obj = {};
    js_obj.foo = wasm_obj;
    assertSame(wasm_obj, js_obj.foo);
    js_obj[0] = wasm_obj;
    assertSame(wasm_obj, js_obj[0]);
  }

  assertEquals(42, wasm_obj ? 42 : 0);

  assertFalse(Array.isArray(wasm_obj));
  assertThrows(() => wasm_obj(), TypeError);
  assertThrows(() => ++wasm_obj, TypeError);
  assertThrows(() => wasm_obj--, TypeError);

  (function* test() { yield wasm_obj; })();
  (function* test() { yield* wasm_obj; })();
  assertThrows(() => [...wasm_obj], TypeError);
  assertEquals({}, {...wasm_obj});
  ((...wasm_obj) => {})();
  assertSame(wasm_obj, ({wasm_obj}).wasm_obj);
  assertThrows(() => ({[wasm_obj]: null}), TypeError);
  assertThrows(() => `${wasm_obj}`, TypeError);
  assertThrows(() => wasm_obj`test`, TypeError);
  assertThrows(() => new wasm_obj, TypeError);
  assertThrows(() => wasm_obj?.property, TypeError);
  assertThrows(() => +wasm_obj, TypeError);
  assertThrows(() => -wasm_obj, TypeError);
  assertThrows(() => ~wasm_obj, TypeError);
  assertFalse(!wasm_obj);
  assertEquals(undefined, void wasm_obj);
  assertThrows(() => wasm_obj - 2, TypeError);
  assertThrows(() => wasm_obj * 2, TypeError);
  assertThrows(() => wasm_obj / 2, TypeError);
  assertThrows(() => wasm_obj ** 2, TypeError);
  assertThrows(() => wasm_obj << 2, TypeError);
  assertThrows(() => wasm_obj >> 2, TypeError);
  assertThrows(() => 2 >>> wasm_obj, TypeError);
  assertThrows(() => 2 % wasm_obj, TypeError);
  assertThrows(() => 2 == wasm_obj, TypeError);
  assertTrue(wasm_obj == wasm_obj);
  assertTrue(wasm_obj === wasm_obj);
  assertFalse(wasm_obj != wasm_obj);
  assertFalse(wasm_obj !== wasm_obj);
  assertFalse(struct == array);
  assertTrue(struct != array);
  assertThrows(() => wasm_obj < wasm_obj, TypeError);
  assertThrows(() => wasm_obj <= wasm_obj, TypeError);
  assertThrows(() => wasm_obj >= wasm_obj, TypeError);
  assertThrows(() => wasm_obj | 1, TypeError);
  assertThrows(() => 1 & wasm_obj, TypeError);
  assertThrows(() => wasm_obj ^ wasm_obj, TypeError);
  assertTrue(wasm_obj? true : false);
  assertThrows(() => wasm_obj += 1, TypeError);
  let tmp = 1;
  assertThrows(() => tmp += wasm_obj, TypeError);
  assertThrows(() => tmp <<= wasm_obj, TypeError);
  assertThrows(() => tmp &= wasm_obj, TypeError);
  assertThrows(() => tmp **= wasm_obj, TypeError);
  assertSame(wasm_obj, tmp &&= wasm_obj);
  tmp = 0;
  assertSame(wasm_obj, tmp ||= wasm_obj);
  tmp = null;
  assertSame(wasm_obj, tmp ??= wasm_obj);
  assertThrows(() => { let [] = wasm_obj; }, TypeError);
  assertThrows(() => { let [a, b] = wasm_obj; }, TypeError);
  assertThrows(() => { let [...all] = wasm_obj; }, TypeError);
  assertThrows(() => { let {a} = wasm_obj; }, TypeError);
  let {} = wasm_obj;
  let {...rest} = wasm_obj;
  assertTrue(rest instanceof Object);
  assertThrows(() => {with (wasm_obj) test;}, ReferenceError);
  with (wasm_obj) var with_lookup = tmp;
  assertEquals(tmp, with_lookup);
  switch (wasm_obj) {
    case 0:
    default:
      throw 1;
    case wasm_obj:
      break;
  }
  try {
    throw wasm_obj;
  } catch(e) {
    assertEquals(e, wasm_obj);
  }

  assertThrows(() => {class SubClass extends wasm_obj {}}, TypeError,
      "Class extends value [object Object] is not a constructor or null");
  class TestMemberInit { x = wasm_obj; };
  assertSame(wasm_obj, new TestMemberInit().x);
  assertSame(wasm_obj, eval("wasm_obj"));

  // Test functions of the global object.
  assertThrows(() => isFinite(wasm_obj), TypeError);
  assertThrows(() => isNaN(wasm_obj), TypeError);
  assertThrows(() => parseFloat(wasm_obj), TypeError);
  assertThrows(() => parseInt(wasm_obj), TypeError);
  assertThrows(() => decodeURI(wasm_obj), TypeError);
  assertThrows(() => decodeURIComponent(wasm_obj), TypeError);
  assertThrows(() => encodeURI(wasm_obj), TypeError);
  assertThrows(() => encodeURIComponent(wasm_obj), TypeError);
  // Test constructors of the global object as function.
  assertThrows(() => AggregateError(wasm_obj), TypeError);
  assertSame(wasm_obj, Array(wasm_obj)[0]);
  assertThrows(() => ArrayBuffer(wasm_obj), TypeError);
  assertThrows(() => BigInt(wasm_obj), TypeError);
  assertThrows(() => BigInt64Array(wasm_obj), TypeError);
  assertThrows(() => BigUint64Array(wasm_obj), TypeError);
  assertEquals(true, Boolean(wasm_obj));
  assertThrows(() => DataView(wasm_obj), TypeError);
  let date = Date(wasm_obj);
  assertEquals("string", typeof date);
  assertThrows(() => Error(wasm_obj), TypeError);
  assertThrows(() => EvalError(wasm_obj), TypeError);
  assertThrows(() => EvalError(wasm_obj), TypeError);
  assertThrows(() => Float64Array(wasm_obj), TypeError);
  assertThrows(() => Function(wasm_obj), TypeError);
  assertThrows(() => Int8Array(wasm_obj), TypeError);
  assertThrows(() => Int16Array(wasm_obj), TypeError);
  assertThrows(() => Int32Array(wasm_obj), TypeError);
  assertThrows(() => Map(wasm_obj), TypeError);
  assertThrows(() => Number(wasm_obj), TypeError);
  assertSame(wasm_obj, Object(wasm_obj));
  assertThrows(() => Promise(wasm_obj), TypeError);
  assertThrows(() => Proxy(wasm_obj), TypeError);
  assertThrows(() => RangeError(wasm_obj), TypeError);
  assertThrows(() => ReferenceError(wasm_obj), TypeError);
  assertThrows(() => RegExp(wasm_obj), TypeError);
  assertThrows(() => Set(wasm_obj), TypeError);
  assertThrows(() => SharedArrayBuffer(wasm_obj), TypeError);
  assertThrows(() => String(wasm_obj), TypeError);
  assertThrows(() => Symbol(wasm_obj), TypeError);
  assertThrows(() => SyntaxError(wasm_obj), TypeError);
  assertThrows(() => TypeError(wasm_obj), TypeError);
  assertThrows(() => Uint8Array(wasm_obj), TypeError);
  assertThrows(() => Uint16Array(wasm_obj), TypeError);
  assertThrows(() => Uint32Array(wasm_obj), TypeError);
  assertThrows(() => URIError(wasm_obj), TypeError);
  assertThrows(() => WeakMap(wasm_obj), TypeError);
  assertThrows(() => WeakRef(wasm_obj), TypeError);
  assertThrows(() => WeakSet(wasm_obj), TypeError);
  // Test constructors of the global object with new.
  assertThrows(() => new AggregateError(wasm_obj), TypeError);
  assertSame(wasm_obj, new Array(wasm_obj)[0]);
  assertThrows(() => new ArrayBuffer(wasm_obj), TypeError);
  assertThrows(() => new BigInt(wasm_obj), TypeError);
  assertThrows(() => new BigInt64Array(wasm_obj), TypeError);
  assertThrows(() => new BigUint64Array(wasm_obj), TypeError);
  assertEquals(true, (new Boolean(wasm_obj)).valueOf());
  assertThrows(() => new DataView(wasm_obj), TypeError);
  assertThrows(() => new Date(wasm_obj), TypeError);
  assertThrows(() => new Error(wasm_obj), TypeError);
  assertThrows(() => new EvalError(wasm_obj), TypeError);
  assertThrows(() => new EvalError(wasm_obj), TypeError);
  assertThrows(() => new Float64Array(wasm_obj), TypeError);
  assertThrows(() => new Function(wasm_obj), TypeError);
  assertThrows(() => new Int8Array(wasm_obj), TypeError);
  assertThrows(() => new Int16Array(wasm_obj), TypeError);
  assertThrows(() => new Int32Array(wasm_obj), TypeError);
  assertThrows(() => new Map(wasm_obj), TypeError);
  assertThrows(() => new Number(wasm_obj), TypeError);
  assertSame(wasm_obj, new Object(wasm_obj));
  assertThrows(() => new Promise(wasm_obj), TypeError);
  assertThrows(() => new Proxy(wasm_obj), TypeError);
  assertThrows(() => new RangeError(wasm_obj), TypeError);
  assertThrows(() => new ReferenceError(wasm_obj), TypeError);
  assertThrows(() => new RegExp(wasm_obj), TypeError);
  assertThrows(() => new Set(wasm_obj), TypeError);
  assertThrows(() => new SharedArrayBuffer(wasm_obj), TypeError);
  assertThrows(() => new String(wasm_obj), TypeError);
  assertThrows(() => new Symbol(wasm_obj), TypeError);
  assertThrows(() => new SyntaxError(wasm_obj), TypeError);
  assertThrows(() => new TypeError(wasm_obj), TypeError);
  assertThrows(() => new Uint8Array(wasm_obj), TypeError);
  assertThrows(() => new Uint16Array(wasm_obj), TypeError);
  assertThrows(() => new Uint32Array(wasm_obj), TypeError);
  assertThrows(() => new URIError(wasm_obj), TypeError);
  assertThrows(() => new WeakMap(wasm_obj), TypeError);
  assertSame(wasm_obj, new WeakRef(wasm_obj).deref());
  assertThrows(() => new WeakSet(wasm_obj), TypeError);

  let tgt = {};
  Object.assign(tgt, wasm_obj);
  assertEquals({}, tgt);
  assertThrows(() => Object.create(wasm_obj), TypeError);
  assertThrows(() => ({}).__proto__ = wasm_obj);
  assertThrows(() => Object.defineProperties(wasm_obj, {prop: {value: 1}}),
      TypeError);
  assertThrows(() => Object.defineProperty(wasm_obj, "prop", {value: 1}),
      TypeError);
  assertThrows(() => Object.fromEntries(wasm_obj), TypeError);
  // FIXME(mliedtke): This should throw according to https://docs.google.com/document/d/17hCQXOyeSgogpJ0I0wir4LRmdvu4l7Oca6e1NkbVN8M/.
  assertEquals(null, Object.getPrototypeOf(wasm_obj));
  assertFalse(Object.hasOwn(wasm_obj, "test"));
  assertThrows(() => Object.preventExtensions(wasm_obj), TypeError);
  assertThrows(() => Object.setPrototypeOf(wasm_obj, Object), TypeError);
  assertEquals([], Object.values(wasm_obj));
  assertThrows(() => wasm_obj.toString(), TypeError);

  {
    let fct = function(x) { return [this, x] };
    assertEquals([wasm_obj, 1], fct.apply(wasm_obj, [1]));
    assertEquals([new Number(1), wasm_obj], fct.apply(1, [wasm_obj]));
    assertThrows(() => fct.apply(1, wasm_obj), TypeError);
    assertEquals([wasm_obj, 1], fct.bind(wasm_obj)(1))
    assertEquals([wasm_obj, 1], fct.call(wasm_obj, 1));
  }

  assertThrows(() => Symbol.for(wasm_obj), TypeError);
  assertThrows(() => Symbol.keyFor(wasm_obj), TypeError);
  assertFalse(Number.isFinite(wasm_obj));
  assertFalse(Number.isInteger(wasm_obj));
  assertFalse(Number.isNaN(wasm_obj));
  assertFalse(Number.isSafeInteger(wasm_obj));
  assertThrows(() => Number.parseFloat(wasm_obj), TypeError);
  assertThrows(() => Number.parseInt(wasm_obj), TypeError);
  assertThrows(() => BigInt.asIntN(2, wasm_obj), TypeError);
  assertThrows(() => BigInt.asUintN(wasm_obj, new BigInt(123)), TypeError);
  assertThrows(() => Math.abs(wasm_obj), TypeError);
  assertThrows(() => Math.acos(wasm_obj), TypeError);
  assertThrows(() => Math.acosh(wasm_obj), TypeError);
  assertThrows(() => Math.asin(wasm_obj), TypeError);
  assertThrows(() => Math.asinh(wasm_obj), TypeError);
  assertThrows(() => Math.atan(wasm_obj), TypeError);
  assertThrows(() => Math.atanh(wasm_obj), TypeError);
  assertThrows(() => Math.atan2(wasm_obj), TypeError);
  assertThrows(() => Math.cbrt(wasm_obj), TypeError);
  assertThrows(() => Math.ceil(wasm_obj), TypeError);
  assertThrows(() => Math.clz32(wasm_obj), TypeError);
  assertThrows(() => Math.cos(wasm_obj), TypeError);
  assertThrows(() => Math.cosh(wasm_obj), TypeError);
  assertThrows(() => Math.exp(wasm_obj), TypeError);
  assertThrows(() => Math.expm1(wasm_obj), TypeError);
  assertThrows(() => Math.floor(wasm_obj), TypeError);
  assertThrows(() => Math.fround(wasm_obj), TypeError);
  assertThrows(() => Math.hypot(wasm_obj), TypeError);
  assertThrows(() => Math.imul(wasm_obj, wasm_obj), TypeError);
  assertThrows(() => Math.log(wasm_obj), TypeError);
  assertThrows(() => Math.log1p(wasm_obj), TypeError);
  assertThrows(() => Math.log10(wasm_obj), TypeError);
  assertThrows(() => Math.log2(wasm_obj), TypeError);
  assertThrows(() => Math.max(2, wasm_obj), TypeError);
  assertThrows(() => Math.min(2, wasm_obj), TypeError);
  assertThrows(() => Math.pow(2, wasm_obj), TypeError);
  assertThrows(() => Math.pow(wasm_obj, 2), TypeError);
  assertThrows(() => Math.round(wasm_obj), TypeError);
  assertThrows(() => Math.sign(wasm_obj), TypeError);
  assertThrows(() => Math.sin(wasm_obj), TypeError);
  assertThrows(() => Math.sinh(wasm_obj), TypeError);
  assertThrows(() => Math.sqrt(wasm_obj), TypeError);
  assertThrows(() => Math.tan(wasm_obj), TypeError);
  assertThrows(() => Math.tanh(wasm_obj), TypeError);
  assertThrows(() => Math.trunc(wasm_obj), TypeError);
  assertThrows(() => Date.parse(wasm_obj), TypeError);
  assertThrows(() => Date.UTC(wasm_obj), TypeError);
  assertThrows(() => (new Date()).setDate(wasm_obj), TypeError);
  assertThrows(() => (new Date()).setFullYear(wasm_obj), TypeError);
  assertThrows(() => (new Date()).setHours(wasm_obj), TypeError);
  assertThrows(() => (new Date()).setMilliseconds(wasm_obj), TypeError);
  assertThrows(() => (new Date()).setMinutes(wasm_obj), TypeError);
  assertThrows(() => (new Date()).setMonth(wasm_obj), TypeError);
  assertThrows(() => (new Date()).setSeconds(wasm_obj), TypeError);
  assertThrows(() => (new Date()).setTime(wasm_obj), TypeError);
  assertThrows(() => (new Date()).setUTCDate(wasm_obj), TypeError);
  assertThrows(() => (new Date()).setUTCFullYear(wasm_obj), TypeError);
  assertThrows(() => (new Date()).setUTCHours(wasm_obj), TypeError);
  assertThrows(() => (new Date()).setUTCMilliseconds(wasm_obj), TypeError);
  assertThrows(() => (new Date()).setUTCMinutes(wasm_obj), TypeError);
  assertThrows(() => (new Date()).setUTCMonth(wasm_obj), TypeError);
  assertThrows(() => (new Date()).setUTCSeconds(wasm_obj), TypeError);
  // FIXME(mliedtke): Shouldn't this throw?
  print((new Date()).toJSON(wasm_obj));
  assertThrows(() => String.fromCharCode(wasm_obj), TypeError);
  assertThrows(() => String.fromCodePoint(wasm_obj), TypeError);
  assertThrows(() => String.raw(wasm_obj), TypeError);
  assertThrows(() => "test".at(wasm_obj), TypeError);
  assertThrows(() => "test".charAt(wasm_obj), TypeError);
  assertThrows(() => "test".charCodeAt(wasm_obj), TypeError);
  assertThrows(() => "test".codePointAt(wasm_obj), TypeError);
  assertThrows(() => "test".concat(wasm_obj), TypeError);
  assertThrows(() => "test".endsWith(wasm_obj), TypeError);
  assertThrows(() => "test".endsWith("t", wasm_obj), TypeError);
  assertThrows(() => "test".includes(wasm_obj), TypeError);
  assertThrows(() => "test".includes("t", wasm_obj), TypeError);
  assertThrows(() => "test".indexOf(wasm_obj), TypeError);
  assertThrows(() => "test".lastIndexOf(wasm_obj), TypeError);
  assertThrows(() => "test".localeCompare(wasm_obj), TypeError);
  assertThrows(() => "test".match(wasm_obj), TypeError);
  assertThrows(() => "test".matchAll(wasm_obj), TypeError);
  assertThrows(() => "test".normalize(wasm_obj), TypeError);
  assertThrows(() => "test".padEnd(wasm_obj), TypeError);
  assertThrows(() => "test".padStart(10, wasm_obj), TypeError);
  assertThrows(() => "test".repeat(wasm_obj), TypeError);
  assertThrows(() => "test".replace(wasm_obj, ""), TypeError);
  assertThrows(() => "test".replace("t", wasm_obj), TypeError);
  assertThrows(() => "test".replaceAll(wasm_obj, ""), TypeError);
  assertThrows(() => "test".search(wasm_obj), TypeError);
  assertThrows(() => "test".slice(wasm_obj, 2), TypeError);
  assertThrows(() => "test".split(wasm_obj), TypeError);
  assertThrows(() => "test".startsWith(wasm_obj), TypeError);
  assertThrows(() => "test".substring(wasm_obj), TypeError);
  assertThrows(() => Array.from(wasm_obj), TypeError);
  assertFalse(Array.isArray(wasm_obj));
  assertEquals([wasm_obj], Array.of(wasm_obj));
  assertThrows(() => [1, 2].at(wasm_obj), TypeError);
  assertEquals([1, wasm_obj],[1].concat(wasm_obj));
  assertThrows(() => [1, 2].copyWithin(wasm_obj), TypeError);
  assertThrows(() => [1, 2].every(wasm_obj), TypeError);
  assertEquals([1, wasm_obj, 3],[1, 2, 3].fill(wasm_obj, 1, 2));
  assertThrows(() => [1, 2].filter(wasm_obj), TypeError);
  assertEquals([wasm_obj], [undefined, wasm_obj, null].filter(
      function(v) { return v == this; }, wasm_obj));
  assertThrows(() => [1, 2].find(wasm_obj), TypeError);
  assertThrows(() => [1, 2].findIndex(wasm_obj), TypeError);
  assertThrows(() => [1, 2].findLast(wasm_obj), TypeError);
  assertThrows(() => [1, 2].findLastIndex(wasm_obj), TypeError);
  assertThrows(() => [1, 2].flat(wasm_obj), TypeError);
  assertThrows(() => [1, 2].flatMap(wasm_obj), TypeError);
  assertThrows(() => [1, 2].forEach(wasm_obj), TypeError);
  {
    let res = [];
    [1, 2].forEach(function(x) {res.push(this); }, wasm_obj);
    assertEquals([wasm_obj, wasm_obj], res);
  }
  assertTrue([wasm_obj].includes(wasm_obj));
  assertFalse([1].includes(wasm_obj));
  assertEquals(1, [0, wasm_obj, 2].indexOf(wasm_obj));
  assertThrows(() => ["a", "b"].join(wasm_obj), TypeError);
  assertEquals(1, [0, wasm_obj, 2].lastIndexOf(wasm_obj));
  assertThrows(() => [1, 2].map(wasm_obj), TypeError);
  assertEquals([wasm_obj, wasm_obj], [1, 2].map(
      function () { return this; }, wasm_obj));
  {
    let arr = [1];
    arr.push(wasm_obj, 3);
    assertEquals([1, wasm_obj, 3], arr);
  }
  assertThrows(() => [1, 2].reduce(wasm_obj), TypeError);
  assertSame(wasm_obj, [].reduce(() => null, wasm_obj));
  assertThrows(() => [1, 2].reduceRight(wasm_obj), TypeError);
  assertThrows(() => [1, 2].slice(wasm_obj, 2), TypeError);
  assertThrows(() => [1, 2].some(wasm_obj), TypeError);
  assertThrows(() => [1, 2].sort(wasm_obj), TypeError);
  assertThrows(() => [1, 2].splice(1, wasm_obj), TypeError);
  {
    let arr = [1, 2];
    arr.unshift(wasm_obj);
    assertEquals([wasm_obj, 1, 2], arr);
  }
  assertThrows(() => Int8Array.from(wasm_obj), TypeError);
  assertThrows(() => Int8Array.of(wasm_obj), TypeError);
  for (let ArrayType of [
    Int8Array, Int16Array, Int32Array, Uint8Array, Uint16Array, Uint32Array]) {
    let array = ArrayType.of(1, 2, 3);
    assertThrows(() => array.at(wasm_obj), TypeError);
    assertThrows(() => array.copyWithin(wasm_obj), TypeError);
    assertThrows(() => array.fill(wasm_obj, 0, 1), TypeError);
    assertThrows(() => array.filter(wasm_obj), TypeError);
    assertThrows(() => array.find(wasm_obj), TypeError);
    assertThrows(() => array.findIndex(wasm_obj), TypeError);
    assertThrows(() => array.findLast(wasm_obj), TypeError);
    assertThrows(() => array.findLastIndex(wasm_obj), TypeError);
    assertThrows(() => array.forEach(wasm_obj), TypeError);
    assertFalse(array.includes(wasm_obj));
    assertEquals(-1, array.indexOf(wasm_obj));
    assertThrows(() => array.join(wasm_obj), TypeError);
    assertEquals(-1, array.lastIndexOf(wasm_obj));
    assertThrows(() => array.map(wasm_obj), TypeError);
    assertThrows(() => array.map(() => wasm_obj), TypeError);
    assertThrows(() => array.reduce(wasm_obj), TypeError);
    assertThrows(() => array.reduceRight(wasm_obj), TypeError);
    assertThrows(() => array.set(wasm_obj), TypeError);
    assertThrows(() => array.set([wasm_obj]), TypeError);
    assertThrows(() => array.slice(wasm_obj, 1), TypeError);
    assertThrows(() => array.some(wasm_obj), TypeError);
    assertThrows(() => array.sort(wasm_obj), TypeError);
    assertThrows(() => array.subarray(0, wasm_obj), TypeError);
  }
  for (let MapType of [Map, WeakMap]) {
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
  }
  for (let SetType of [Set, WeakSet]) {
    let set = new SetType([new String("a"), wasm_obj]);
    set.add(wasm_obj);
    assertTrue(set.has(wasm_obj));
    set.delete(wasm_obj);
    assertFalse(set.has(wasm_obj));
  }
  assertFalse(ArrayBuffer.isView(wasm_obj));
  assertThrows(
      () => (new ArrayBuffer(32)).slice(wasm_obj, wasm_obj), TypeError);
  assertThrows(
      () => (new SharedArrayBuffer(32)).slice(wasm_obj, wasm_obj), TypeError);
  let arrayBuf = new ArrayBuffer(32);
  let dataView = new DataView(arrayBuf);
  assertThrows(() => dataView.getBigInt64(wasm_obj), TypeError);
  assertThrows(() => dataView.getBigUint64(wasm_obj), TypeError);
  assertThrows(() => dataView.getFloat32(wasm_obj), TypeError);
  assertThrows(() => dataView.getFloat64(wasm_obj), TypeError);
  assertThrows(() => dataView.getInt8(wasm_obj), TypeError);
  assertThrows(() => dataView.getInt16(wasm_obj), TypeError);
  assertThrows(() => dataView.getInt32(wasm_obj), TypeError);
  assertThrows(() => dataView.getUint8(wasm_obj), TypeError);
  assertThrows(() => dataView.getUint16(wasm_obj), TypeError);
  assertThrows(() => dataView.getUint32(wasm_obj), TypeError);
  assertThrows(() => dataView.setBigInt64(wasm_obj), TypeError);
  assertThrows(() => dataView.setBigUint64(wasm_obj), TypeError);
  assertThrows(() => dataView.setFloat32(wasm_obj), TypeError);
  assertThrows(() => dataView.setFloat64(0, wasm_obj), TypeError);
  assertThrows(() => dataView.setInt8(wasm_obj), TypeError);
  assertThrows(() => dataView.setInt16(0, wasm_obj), TypeError);
  assertThrows(() => dataView.setInt32(wasm_obj), TypeError);
  assertThrows(() => dataView.setUint8(0, wasm_obj), TypeError);
  assertThrows(() => dataView.setUint16(wasm_obj), TypeError);
  assertThrows(() => dataView.setUint32(0, wasm_obj), TypeError);

  let i8Array = new Int8Array(32);
  assertThrows(() => Atomics.add(wasm_obj, 1, 2), TypeError);
  assertThrows(() => Atomics.add(i8Array, wasm_obj, 2), TypeError);
  assertThrows(() => Atomics.add(i8Array, 1, wasm_obj), TypeError);
  assertThrows(() => Atomics.and(wasm_obj, 1, 2), TypeError);
  assertThrows(() => Atomics.and(i8Array, wasm_obj, 2), TypeError);
  assertThrows(() => Atomics.and(i8Array, 1, wasm_obj), TypeError);
  assertThrows(() => Atomics.compareExchange(wasm_obj, 1, 2, 3), TypeError);
  assertThrows(() => Atomics.compareExchange(i8Array, wasm_obj, 2, 3), TypeError);
  assertThrows(() => Atomics.compareExchange(i8Array, 1, wasm_obj, 3), TypeError);
  assertThrows(() => Atomics.compareExchange(i8Array, 1, 2, wasm_obj), TypeError);
  assertThrows(() => Atomics.exchange(wasm_obj, 1, 2), TypeError);
  assertThrows(() => Atomics.exchange(i8Array, wasm_obj, 2), TypeError);
  assertThrows(() => Atomics.exchange(i8Array, 1, wasm_obj), TypeError);
  assertThrows(() => Atomics.isLockFree(wasm_obj), TypeError);
  assertThrows(() => Atomics.load(wasm_obj, 1), TypeError);
  assertThrows(() => Atomics.load(i8Array, wasm_obj), TypeError);
  assertThrows(() => Atomics.or(wasm_obj, 1, 2), TypeError);
  assertThrows(() => Atomics.or(i8Array, wasm_obj, 2), TypeError);
  assertThrows(() => Atomics.or(i8Array, 1, wasm_obj), TypeError);
  assertThrows(() => Atomics.store(wasm_obj, 1, 2), TypeError);
  assertThrows(() => Atomics.store(i8Array, wasm_obj, 2), TypeError);
  assertThrows(() => Atomics.store(i8Array, 1, wasm_obj), TypeError);
  assertThrows(() => Atomics.sub(wasm_obj, 1, 2), TypeError);
  assertThrows(() => Atomics.sub(i8Array, wasm_obj, 2), TypeError);
  assertThrows(() => Atomics.sub(i8Array, 1, wasm_obj), TypeError);
  assertThrows(() => Atomics.wait(wasm_obj, 1, 2, 3), TypeError);
  assertThrows(() => Atomics.wait(i8Array, wasm_obj, 2, 3), TypeError);
  assertThrows(() => Atomics.wait(i8Array, 1, wasm_obj, 3), TypeError);
  assertThrows(() => Atomics.wait(i8Array, 1, 2, wasm_obj), TypeError);
  assertThrows(() => Atomics.notify(wasm_obj, 1, 2), TypeError);
  assertThrows(() => Atomics.notify(i8Array, wasm_obj, 2), TypeError);
  assertThrows(() => Atomics.notify(i8Array, 1, wasm_obj), TypeError);
  assertThrows(() => Atomics.xor(wasm_obj, 1, 2), TypeError);
  assertThrows(() => Atomics.xor(i8Array, wasm_obj, 2), TypeError);
  assertThrows(() => Atomics.xor(i8Array, 1, wasm_obj), TypeError);

  assertThrows(() => JSON.parse(wasm_obj), TypeError);
  assertEquals({x: 1}, JSON.parse('{"x": 1}', wasm_obj));
  assertThrows(() => JSON.stringify(wasm_obj), TypeError);
  assertEquals('{"x":1}', JSON.stringify({x: 1}, wasm_obj));
  assertEquals('{"x":1}', JSON.stringify({x: 1}, null, wasm_obj));

  assertThrowsAsync(Promise.all(wasm_obj), TypeError);
  assertThrowsAsync(Promise.all([wasm_obj]), TypeError);
  assertThrowsAsync(Promise.allSettled(wasm_obj), TypeError);
  Promise.allSettled([wasm_obj]).then((info) => assertEquals("rejected", info[0].status));
  assertThrowsAsync(Promise.any(wasm_obj), TypeError);
  assertThrowsAsync(Promise.any([wasm_obj]), AggregateError);
  assertThrowsAsync(Promise.race(wasm_obj), TypeError);
  assertThrowsAsync(Promise.race([wasm_obj]), TypeError);
  // TODO(7748): Allow WebAssembly objects as Promise resolve / reject arg.
  assertThrowsAsync(new Promise((resolve, reject) => resolve(wasm_obj)), TypeError);
  // TODO(7748): The assertThrowsAsync doesn't work correctly with rejected
  // promise and expectated error type.
  assertThrowsAsync(new Promise((resolve, reject) => reject(wasm_obj)));
  // If the `then` argument isn't a callback, it will simply be replaced with
  // an identity function (x) => x.
  (new Promise((resolve) => resolve({})))
  .then(wasm_obj) // The value itself doesn't have any impact.
  .then((v) => assertEquals({}, v),
        () => assertTrue(false));
  // If the `catch` argument isn't a callback, it will be replaced with a
  // thrower function (x) => { throw x; }.
  (new Promise((resolve, reject) => reject({}))).then(() => null)
  .catch(wasm_obj) // The value itself doesn't have any impact.
  .then(() => assertTrue(false),
        (v) => assertEquals({}, v));
  // `finally(wasm_obj)` behaves just like `then(wasm_obj, wasm_obj)`
  (new Promise((resolve, reject) => resolve({})))
  .finally(wasm_obj)
  .then((v) => assertEquals({}, v),
        () => assertTrue(false));
  (new Promise((resolve, reject) => reject({})))
  .finally(wasm_obj)
  .then(() => assertTrue(false),
        (v) => assertEquals({}, v));

  // TODO(7748): Using a wasm_obj as a resolved value in a then() chain
  // leads to a Type error.
  (new Promise((resolve) => resolve({})))
  .then(() => wasm_obj)
  .then(() => assertTrue(false),
        (v) => assertTrue(v instanceof TypeError));

  // Yielding wasm objects from a generator function is valid.
  let gen = (function* () { yield wasm_obj; })();
  assertSame(wasm_obj, gen.next().value);
  // Test passing wasm objects via next() back to a generator function.
  let gen2 = (function* () { assertSame(wasm_obj, yield 1); })();
  assertEquals(1, gen2.next().value);
  assertTrue(gen2.next(wasm_obj).done); // triggers the assertEquals.
  // Test passing wasm objects via return() to a generator function.
  let gen3 = (function* () { yield 1; assertTrue(false); })();
  assertEquals({value: wasm_obj, done: true}, gen3.return(wasm_obj));
  // Test passing wasm objects via throw() to a generator function.
  let gen4 = (function* () {
    try {
      yield 1;
      assertTrue(false); // unreached
    } catch (e) {
      assertSame(wasm_obj, e);
      return 2;
    }
  })();
  assertEquals(1, gen4.next().value);
  gen4.throw(wasm_obj); // wasm_obj is caught inside the generator
  // Treating wasm objects as generators is invalid.
  let gen5 = function* () { yield *wasm_obj; }
  assertThrows(() => gen5().next(), TypeError);

  // FIXME(mliedtke): Should we repeat the same testing for async generator
  // functions as for the synchronous ones?

  {
    let fct = function(x) { return [this, x] };
    assertEquals([wasm_obj, 1], Reflect.apply(fct, wasm_obj, [1]));
    assertEquals([{}, wasm_obj], Reflect.apply(fct, {}, [wasm_obj]));
    assertThrows(() => Reflect.apply(fct, 1, wasm_obj));
    assertThrows(() => Reflect.apply(wasm_obj, null, []), TypeError);
  }
  assertThrows(() => Reflect.construct(wasm_obj, []), TypeError);
  assertThrows(() => Reflect.construct(Object, wasm_obj), TypeError);
  assertThrows(() => Reflect.construct(Object, [], wasm_obj), TypeError);
  assertThrows(() => Reflect.defineProperty(wasm_obj, "prop", {value: 1}),
               TypeError);
  assertThrows(() => Reflect.defineProperty({}, wasm_obj, {value: 1}),
               TypeError);
  {
    let obj = {};
    // The behavior of returning true and not throwing a type error is
    // consistent with passing {}.
    // FIXME(mliedtke): Should this throw?
    assertTrue(Reflect.defineProperty(obj, "prop", wasm_obj));
    assertTrue(obj.hasOwnProperty("prop"));
    assertEquals(undefined, obj.prop);
    assertTrue(Reflect.defineProperty(obj, "prop2", {value: wasm_obj}));
    assertSame(wasm_obj, obj.prop2);
  }
  assertThrows(() => Reflect.deleteProperty(wasm_obj, "prop"),
                 TypeError);
  assertThrows(() => Reflect.deleteProperty({}, wasm_obj),
                TypeError);
  assertThrows(() => Reflect.get(wasm_obj, "prop"),
                TypeError);

  // Ensure no statement re-asssigned wasm_obj by accident.
  assertTrue(wasm_obj == struct || wasm_obj == array);
}

(async function testAsync() {
  for (let wasm_obj of [struct, array]) {
    try {
      await wasm_obj;
      throw "unreachable";
    } catch (e) {
      assertTrue(e instanceof TypeError);
    }
  }
})();

// TODO(mliedtke): What about `export {struct, array}`?
// d8 doesn't seem to accept exports (as the file isn't a module?)
