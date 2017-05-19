// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

// Check that %WasmNumInterpretedCalls and %RedirectToWasmInterpreter do not
// crash on illegal arguments.

assertThrows("%WasmNumInterpretedCalls()", SyntaxError);
assertThrows("%WasmNumInterpretedCalls(1, 2)", SyntaxError);
assertEquals(undefined, %WasmNumInterpretedCalls(1));
assertEquals(undefined, %WasmNumInterpretedCalls({}));

assertThrows("%RedirectToWasmInterpreter()", SyntaxError);
assertThrows("%RedirectToWasmInterpreter(1)", SyntaxError);
assertEquals(undefined, %RedirectToWasmInterpreter(1, 2));
assertEquals(undefined, %RedirectToWasmInterpreter({}, 3));
assertEquals(undefined, %RedirectToWasmInterpreter({}, {}));
