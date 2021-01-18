// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test/cctest/test-swiss-hash-table-shared-tests.h"

namespace v8 {
namespace internal {
namespace test_swiss_hash_table {

// Executes the tests in test-swiss-hash-table-shared.cc as if they were defined
// in this file, using the CSATestRunner.
const char kCSATestFileName[] = __FILE__;
SharedSwissTableTests<CSATestRunner, kCSATestFileName> execute_shared_tests_csa;

}  // namespace test_swiss_hash_table
}  // namespace internal
}  // namespace v8
