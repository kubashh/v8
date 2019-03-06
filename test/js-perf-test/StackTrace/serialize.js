// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

(function() {

// "errors" contains all the error thrown and caught by the "setup" function.
const kErrorCount = 100000;
let errors;
let counter;

function ThrowAndCatchSetup(fn) {
  counter = 0;
  errors = [];
  for (let i = 0; i < kErrorCount; ++i) {
    try {
      fn();
    } catch (err) {
      errors[i] = err;
    }
  }
}

function SimpleSetup() {
  ThrowAndCatchSetup(() => { throw new Error("Simple Error"); });
}

class CustomError extends Error {};
function CustomSetup() {
  ThrowAndCatchSetup(() => { throw new CustomError("Custom Error"); });
}

function InlineSetup() {
  function Inner() {
    throw new Error("Throwing from inlined function!");
  }
  function Middle() { Inner(); }
  function Outer() { Middle(); }

  try { Outer(); } catch(err) {}
  try { Outer(); } catch(err) {}
  %OptimizeFunctionOnNextCall(Outer);
  try { Outer(); } catch(err) {}

  ThrowAndCatchSetup(() => Outer());
}

function SerializeStack() {
  if (counter < kErrorCount) {
    // Trigger serialization by accessing Error.stack.
    errors[counter++].stack.charAt(0);
  } else {
    // The counter is reset after hitting the end, although
    // Error.stack is cached at this point.
    counter = 0;
  }
}

createSuite('Simple-Serialize-Error.stack', 1000, SerializeStack, SimpleSetup);
createSuite('Custom-Serialize-Error.stack', 1000, SerializeStack, CustomSetup);

createSuite('Inline-Serialize-Error.stack', 1000, SerializeStack, InlineSetup);

})();
