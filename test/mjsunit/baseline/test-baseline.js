// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --super-ic

function run(f, ...args) {
  try { f(...args); } catch (e) {}
  %CompileBaseline(f);
  return f(...args);
}

function construct(f, ...args) {
  try { new f(...args); } catch (e) {}
  %CompileBaseline(f);
  return new f(...args);
}

// Constants
assertEquals(run(()=>undefined), undefined);
assertEquals(run(()=>null), null);
assertEquals(run(()=>true), true);
assertEquals(run(()=>false), false);
assertEquals(run(()=>"bla"), "bla");
assertEquals(run(()=>42), 42);
assertEquals(run(()=>0), 0);

// Variables
assertEquals(run(()=>{let a = 42; return a}), 42);
assertEquals(run(()=>{let a = 42; let b = 32; return a}), 42);

// Property load
assertEquals(run((o)=>o.a, {a:42}), 42);
assertEquals(run((o, k)=>o[k], {a:42}, "a"), 42);

// Property store
assertEquals(run((o)=>{o.a=42; return o}, {}).a, 42);
assertEquals(run((o, k)=>{o[k]=42; return o}, {}, "a").a, 42);

// Global load/store
global_x = 45;
assertEquals(run(()=>global_x), 45);
run(()=>{ global_x = 49 })
assertEquals(global_x, 49);

// Super
// var o = {__proto__:{a:42}, m() { return super.a }};
// assertEquals(run(o.m), 42);

// Control flow
assertEquals(run((x)=>{ if(x) return 5; return 10;}), 10);
assertEquals(run(()=>{ var x = 0; for(var i = 1; i; i=0) x=10; return x;}), 10);
assertEquals(run(()=>{ var x = 0; for(var i = 0; i < 10; i+=1) x+=1; return x;}), 10);

// Typeof
assertEquals('undefined', run(()=>typeof undefined));
assertEquals('object', run(()=>typeof null));
assertEquals('boolean', run(()=>typeof true));
assertEquals('boolean', run(()=>typeof false));
assertEquals('number', run(()=>typeof 42.42));
assertEquals('number', run(()=>typeof 42));
assertEquals('bigint', run(()=>typeof 42n));
assertEquals('string', run(()=>typeof '42'));
assertEquals('symbol', run(()=>typeof Symbol(42)));
assertEquals('object', run(()=>typeof {}));
assertEquals('object', run(()=>typeof []));
//assertEquals('object', run(()=>typeof new Proxy({}, {})));
//assertEquals('object', run(()=>typeof new Proxy([], {})));
assertEquals('function', run(()=>typeof (_ => 42)));
assertEquals('function', run(()=>typeof function() {}));
assertEquals('function', run(()=>typeof function*() {}));
assertEquals('function', run(()=>typeof async function() {}));
assertEquals('function', run(()=>typeof async function*() {}));
//assertEquals('function', run(()=>typeof new Proxy(_ => 42, {})));
//assertEquals('function', run(()=>typeof class {}));
assertEquals('function', run(()=>typeof Object));

// Binop
assertEquals(run((a,b)=>{return a+b}, 41, 1), 42);
assertEquals(run((a,b)=>{return a*b}, 21, 2), 42);
assertEquals(run((a)=>{return a+3}, 39), 42);

// Unop
//assertEquals(run((x)=>{return x++}, 41), 42);
assertEquals(run((x)=>{return ++x}, 41), 42);
//assertEquals(run((x)=>{return x--}, 41), 40);
assertEquals(run((x)=>{return --x}, 41), 40);
assertEquals(run((x)=>{return !x}, 41), false);
assertEquals(run((x)=>{return ~x}, 41), ~41);

// Calls
function f0() { return 42; }
function f1(x) { return x; }
function f2(x, y) { return x + y; }
function f3(x, y, z) { return y + z; }
assertEquals(run(()=>{return f0()}), 42);
assertEquals(run(()=>{return f1(42)}), 42);
assertEquals(run(()=>{return f2(41, 1)}), 42);
assertEquals(run(()=>{return f3(1, 2, 40)}), 42);

// Property call
let obj = {
  f0: () => { return 42; },
  f1: (x) => { return x; },
  f2: (x, y) => { return x + y; },
  f3: (x, y, z) => { return y + z; }
}
assertEquals(run(()=>{return obj.f0()}), 42);
assertEquals(run(()=>{return obj.f1(42)}), 42);
assertEquals(run(()=>{return obj.f2(41, 1)}), 42);
assertEquals(run(()=>{return obj.f3(1, 2, 40)}), 42);


// Closure
assertEquals(run((o)=>{if (true) {let x = o; return ()=>x}}, 42)(), 42);
assertEquals(run((o)=>{return ()=>o}, 42)(), 42);

// Object / Array Literals
assertEquals(run((o)=>{return {a:42}}), {a:42});
assertEquals(run((o)=>{return [42]}), [42]);
assertEquals(run((o)=>{return []}), []);
assertEquals(run((o)=>{return {}}), {});
assertEquals(run((o)=>{return {...o}}, {a:42}), {a:42});
assertEquals(run((o)=>{return /42/}), /42/);

// Construct
// TODO(verwaest): Enable once we can actually walk Sparkplug frames.
// Throw if the super() isn't a constructor
// class C extends Object { constructor() { super() } }
// C.__proto__ = null;
// assertThrows(()=>construct(C));
