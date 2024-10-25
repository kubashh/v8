// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

let {session, contextGroup, Protocol} = InspectorTest.start(
  'Check fail compile and recompile');

let _function = `
function func() {
  const a = 1;
}
await new Promise(resolve => {
  func();
  resolve()
});
`;


const url = "file.js";

(
  async function test() {
    Protocol.Debugger.enable();
    await contextGroup.compileFunction(_function, url);

    // no breakpoint
    let result1 = await Protocol.Debugger.setBreakpointByUrl({
        lineNumber: 2,  url
      })

    let hasBreakpoint1 = result1.result.locations.length > 0;
    InspectorTest.log(`hasBreakpoint: ${hasBreakpoint1}`);

    await Protocol.Debugger.removeBreakpoint({breakpointId: result1.result.breakpointId});

    await contextGroup.addModule(_function, url);

    let result2 = await Protocol.Debugger.setBreakpointByUrl({
      lineNumber: 2,  url
    })

    let hasBreakpoint2 = result2.result.locations.length > 0;
    InspectorTest.log(`hasBreakpoint: ${hasBreakpoint2}`);

    InspectorTest.completeTest();
  }
)()
