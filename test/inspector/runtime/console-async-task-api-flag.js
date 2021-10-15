// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

const { session, contextGroup, Protocol } = InspectorTest.start(
  "Checks that async stack tagging APIs are not available without a runtime flag."
);

Protocol.Runtime.onConsoleAPICalled(InspectorTest.logMessage);
Protocol.Runtime.enable();

contextGroup.addScript(`
function checkScheduleAsyncTask() {
  return typeof console.scheduleAsyncTask;
}
function checkStartAsyncTask() {
  return typeof console.startAsyncTask;
}
function checkFinishAsyncTask() {
  return typeof console.finishAsyncTask;
}
function checkCancelAsyncTask() {
  return typeof console.cancelAsyncTask;
}
//# sourceURL=test.js`);

(async () => {
  // await InspectorTest.logMessage(
  //   await Protocol.Runtime.evaluate({
  //     expression: "checkScheduleAsyncTask()//# sourceURL=evaluate.js",
  //   })
  // );
  // await InspectorTest.logMessage(
  //   await Protocol.Runtime.evaluate({
  //     expression: "checkStartAsyncTask()//# sourceURL=evaluate.js",
  //   })
  // );
  // await InspectorTest.logMessage(
  //   await Protocol.Runtime.evaluate({
  //     expression: "checkFinishAsyncTask()//# sourceURL=evaluate.js",
  //   })
  // );
  // await InspectorTest.logMessage(
  //   await Protocol.Runtime.evaluate({
  //     expression: "checkCancelAsyncTask()//# sourceURL=evaluate.js",
  //   })
  // );

  await InspectorTest.completeTest();
})();
