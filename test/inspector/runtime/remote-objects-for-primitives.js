// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

let {session, contextGroup, Protocol} =
    InspectorTest.start("Tests Runtime.RemoteObject.");

InspectorTest.runAsyncTestSuite([
  async function testNull() {
    InspectorTest.logMessage((await Protocol.Runtime.evaluate({
      expression: 'null'
    })).result);
  },
  async function testBoolean() {
    InspectorTest.logMessage((await Protocol.Runtime.evaluate({
      expression: 'true'
    })).result);
    InspectorTest.logMessage((await Protocol.Runtime.evaluate({
      expression: 'false'
    })).result);
  },
  async function testNumber() {
    InspectorTest.log('NaN');
    InspectorTest.logMessage((await Protocol.Runtime.evaluate({
      expression: '0 / {}'
    })).result);
    InspectorTest.log('-0');
    InspectorTest.logMessage((await Protocol.Runtime.evaluate({
      expression: '-0'
    })).result);
    InspectorTest.log('0');
    InspectorTest.logMessage((await Protocol.Runtime.evaluate({
      expression: '0'
    })).result);
    InspectorTest.log('Infinity');
    InspectorTest.logMessage((await Protocol.Runtime.evaluate({
      expression: '1/0'
    })).result);
    InspectorTest.log('-Infinity');
    InspectorTest.logMessage((await Protocol.Runtime.evaluate({
      expression: '-1/0'
    })).result);
    InspectorTest.log('2.3456');
    InspectorTest.logMessage((await Protocol.Runtime.evaluate({
      expression: '2.3456'
    })).result);
  },
  async function testUndefined() {
    InspectorTest.logMessage((await Protocol.Runtime.evaluate({
      expression: 'undefined'
    })).result);
  },
  async function testString() {
    InspectorTest.logMessage((await Protocol.Runtime.evaluate({
      expression: '\'Hello!\''
    })).result);
  },
  async function testReturnByValue() {
    InspectorTest.log('Empty object');
    InspectorTest.logMessage((await Protocol.Runtime.evaluate({
      expression: '({})', returnByValue: true
    })).result);
    InspectorTest.log('Object with properties');
    InspectorTest.logMessage((await Protocol.Runtime.evaluate({
      expression: '({a:1, b:2})', returnByValue: true
    })).result);
    InspectorTest.log('Object with cycle');
    InspectorTest.logMessage((await Protocol.Runtime.evaluate({
      expression: 'a = {};a.a = a; a', returnByValue: true
    })).error);
    InspectorTest.log('Function () => 42');
    InspectorTest.logMessage((await Protocol.Runtime.evaluate({
      expression: '() => 42', returnByValue: true
    })).result);
    InspectorTest.log('Symbol(42)');
    InspectorTest.logMessage((await Protocol.Runtime.evaluate({
      expression: 'Symbol(42)', returnByValue: true
    })).error);
    InspectorTest.log('Error object');
    InspectorTest.logMessage((await Protocol.Runtime.evaluate({
      expression: 'new Error()', returnByValue: true
    })).result);
  }
]);
