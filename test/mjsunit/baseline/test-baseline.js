// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --super-ic

function run(f, ...args) {
  f(...args);
  %CompileBaseline(f);
  return f(...args);
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

// Super
var o = {__proto__:{a:42}, m() { return super.a }};
assertEquals(run(o.m), 42);

// Control flow
assertEquals(run((x)=>{ if(x) return 5; return 10;}), 10);
//assertEquals(run((x)=>{ var x = 0; for(var i = 0; i < 10; i++) x++; return x;}), 10);
assertEquals(run(()=>{ var x = 0; for(var i = 1; i; i=0) x=10; return x;}), 10);
