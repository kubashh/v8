// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

function check_test_results(tests, status) {
  let failed_counter = 0;
  tests.map(function(x) {
    const PASS = 0;
    const FAIL = 1;
    const TIMEOUT = 2;
    const NOTRUN = 3;
    const PRECONDITION_FAILED = 4;

      if (x.status !== PASS) {
        console.log();
        if (x.status === FAIL) {
          console.log("Test failed");
        } else if(x.status === TIMEOUT) {
          console.log("Timeout");
        } else if(x.status === NOTRUN) {
          console.log("Test did not run");
        } else if (x.status === PRECONDITION_FAILED) {
          console.log("Test precondition failed");
        } else {
          console.log("Unknown error code:", x.status);
        }
        console.log("Message:");
        console.log(x.message);
        console.log("Stack trace:");
        console.log(x.stack);
        failed_counter++;
      }
  });
  assertEquals(0, failed_counter);
}

add_completion_callback(check_test_results);
