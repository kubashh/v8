// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

let {session, contextGroup, Protocol} = InspectorTest.start('Tests that error object has description');

InspectorTest.runAsyncTestSuite([
  async function testPromiseReject() {
    InspectorTest.logMessage(await Protocol.Runtime.evaluate({
      expression: 'Promise.reject(new Error(\'asdf\'))',
      returnByValue: true, awaitPromise: true}));
  },

  async function testErrorByValue() {
    InspectorTest.logMessage(await Protocol.Runtime.evaluate({
      expression: 'new Error(\'asdf\')',
      returnByValue: true}));
  },

  async function testError() {
    InspectorTest.logMessage(await Protocol.Runtime.evaluate({
      expression: 'new Error(\'asdf\')',
      returnByValue: false}));
  },
]);
