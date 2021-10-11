// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

const {session, contextGroup, Protocol} = InspectorTest.start(
    'Checks that async stack is captured with the userland API.');

Protocol.Runtime.enable();
Protocol.Runtime.onConsoleAPICalled(
    (message) => InspectorTest.logMessage(message.params.stackTrace));

contextGroup.addScript(`
function test() {
  /* --- Library --- */

  function makeScheduler() {
    let stack = [];

    return {
      scheduleUnitOfWork(f) {
        let id = stack.length;
        console.scheduleAsyncTask("userland", id);
        console.startAsyncTask(id);
        stack.push({ id, f });
      },

      workLoop() {
        while (stack.length) {
          const { id, f } = stack.pop();
          f();
          console.finishAsyncTask(id);
        }
      },
    };
  }

  const scheduler = makeScheduler();

  /* --- Userland --- */

  function someTask() {
    console.trace("completeWork");
  }

  function someOtherTask() {
    console.trace("completeWork");
  }

  function businessLogic() {
    scheduler.scheduleUnitOfWork(someTask);
    scheduler.scheduleUnitOfWork(someOtherTask);
  }

  businessLogic();
  scheduler.workLoop();
}
//# sourceURL=test.js`);

Protocol.Runtime.setAsyncCallStackDepth({maxDepth: 10});
Protocol.Runtime.evaluate({expression: 'test()//# sourceURL=expr.js'})
    .then(InspectorTest.completeTest);
