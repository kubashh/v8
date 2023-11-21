// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --no-always-turbofan --turbofan
// Flags: --no-stress-flush-code
// Flags: --no-stress-incremental-marking
// Flags: --no-concurrent-recompilation
// Flags: --file-coverage-filter="foo.js"
// Flags: --no-maglev

var source =
`
function fib(x) {
  if (x < 2) return 1;
  return fib(x-1) + fib(x-2);
}
function is_optimized(f) {
  return (%GetOptimizationStatus(f) & 16) ? "optimized" : "unoptimized";
}
(function iife() {
  return 1;
})();
fib(5);
`;

let {session, contextGroup, Protocol} = InspectorTest.start("Test collecting code coverage data with Profiler.collectCoverage and filtering.");

function ClearAndGC() {
  return Protocol.Runtime.evaluate({ expression: "fib = g = f = h = is_optimized = null;" })
             .then(GC);
}

function GC() {
  return Protocol.HeapProfiler.collectGarbage();
}

function LogSorted(message) {
  message.result.result.sort((a, b) => parseInt(a.scriptId) - parseInt(b.scriptId));
  return InspectorTest.logMessage(message);
}

InspectorTest.runTestSuite([
  function testPreciseCoverageFilter(next)
  {
    Protocol.Runtime.enable()
      .then(Protocol.Profiler.enable)
      .then(() => Protocol.Profiler.startPreciseCoverage({callCount: true, detailed: false}))
      .then(() => Protocol.Runtime.compileScript({ expression: source, sourceURL: 'file://tmp/foo.js', persistScript: true }))
      .then((result) => Protocol.Runtime.runScript({ scriptId: result.result.scriptId }))
      .then(InspectorTest.logMessage)
      .then(ClearAndGC)
      .then(Protocol.Profiler.takePreciseCoverage)
      .then(LogSorted)
      .then(Protocol.Profiler.takePreciseCoverage)
      .then(LogSorted)
      .then(Protocol.Profiler.stopPreciseCoverage)
      .then(Protocol.Profiler.disable)
      .then(Protocol.Runtime.disable)
      .then(ClearAndGC)
      .then(next);
  },
  function testPreciseCoverageFilterExclude(next)
  {
    Protocol.Runtime.enable()
      .then(Protocol.Profiler.enable)
      .then(() => Protocol.Profiler.startPreciseCoverage({callCount: true, detailed: false}))
      .then(() => Protocol.Runtime.compileScript({ expression: source, sourceURL: arguments.callee.name, persistScript: true }))
      .then((result) => Protocol.Runtime.runScript({ scriptId: result.result.scriptId }))
      .then(InspectorTest.logMessage)
      .then(ClearAndGC)
      .then(Protocol.Profiler.takePreciseCoverage)
      .then(LogSorted)
      .then(Protocol.Profiler.takePreciseCoverage)
      .then(LogSorted)
      .then(Protocol.Profiler.stopPreciseCoverage)
      .then(Protocol.Profiler.disable)
      .then(Protocol.Runtime.disable)
      .then(ClearAndGC)
      .then(next);
  }
]);
