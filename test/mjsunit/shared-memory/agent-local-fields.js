// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --shared-string-table --harmony-struct --allow-natives-syntax

"use strict";

if (this.Worker) {

(function TestAgentLocalFields() {
  let workerScript =
      `onmessage = function(struct) {
         if (struct.agent_local_method !== undefined) {
           postMessage("error");
           return;
         }
         struct.agent_local_method = (() => "worker");
         if (struct.agent_local_method() !== "worker") {
           postMessage("error");
           return;
         }
         postMessage("done");
       };
       postMessage("started");`;

  let worker = new Worker(workerScript, { type: 'string' });
  let started = worker.getMessage();
  assertEquals("started", started);

  let StructWithAgentLocal = new SharedStructType(['agent_local_method'],
                                                  ['agent_local_method']);
  let struct = new StructWithAgentLocal();
  let m = (() => "main");
  struct.agent_local_method = m;
  assertEquals("main", struct.agent_local_method());
  worker.postMessage(struct);
  assertEquals("done", worker.getMessage());
  assertSame(m, struct.agent_local_method);
  assertEquals("main", struct.agent_local_method());

  worker.terminate();
})();

}
