// Copyright 2019 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// Test the performance.measureMemory() function of d8.  This test only makes
// sense with d8.

load('test/mjsunit/mjsunit.js');

function assertLessThanOrEqual(a, b) {
  assertTrue(a <= b, `Expected ${a} <= ${b}`);
}

function checkMeasureMemoryResult(result) {
  assertTrue('total' in result);
  assertTrue('jsMemoryEstimate' in result.total);
  assertTrue('jsMemoryRange' in result.total);
  assertEquals('number', typeof result.total.jsMemoryEstimate);
  assertEquals(2, result.total.jsMemoryRange.length);
  assertEquals('number', typeof result.total.jsMemoryRange[0]);
  assertEquals('number', typeof result.total.jsMemoryRange[1]);
  assertLessThanOrEqual(result.total.jsMemoryRange[0],
                        result.total.jsMemoryRange[1]);
  assertLessThanOrEqual(result.total.jsMemoryRange[0],
                        result.total.jsMemoryEstimate);
  assertLessThanOrEqual(result.total.jsMemoryEstimate,
                        result.total.jsMemoryRange[1]);
}

if (this.performance && performance.measureMemory) {
  assertPromiseResult((async () => {
    let result = await performance.measureMemory();
    checkMeasureMemoryResult(result);
  })());

  assertPromiseResult((async () => {
    let result = await performance.measureMemory({detailed: false});
    checkMeasureMemoryResult(result);
  })());

  assertPromiseResult((async () => {
    let result = await performance.measureMemory({detailed: true});
    // TODO(ulan): Also check the detailed results once measureMemory
    // supports them.
    checkMeasureMemoryResult(result);
  })());
}
