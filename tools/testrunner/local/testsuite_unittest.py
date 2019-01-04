#!/usr/bin/env python
# Copyright 2016 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import sys
import tempfile
import unittest

# Needed because the test runner contains relative imports.
TOOLS_PATH = os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))))
sys.path.append(TOOLS_PATH)

from testrunner.local.testsuite import TestSuite
from testrunner.objects.testcase import TestCase
from testrunner.test_config import TestConfig


class TestSuiteTest(unittest.TestCase):
  def setUp(self):
    test_dir = os.path.dirname(__file__)
    self.test_root = os.path.join(test_dir, "fake_testsuite")
    self.test_config = TestConfig(
        command_prefix=[],
        extra_flags=[],
        isolates=False,
        mode_flags=[],
        no_harness=False,
        noi18n=False,
        random_seed=0,
        run_skipped=False,
        shell_dir='fake_testsuite/fake_d8',
        timeout=10,
        verbose=False,
    )

  def testLoadingTestSuitesFromDisk(self):
    suite = TestSuite.LoadFromDisk(
      self.test_root, self.test_config, statusfile_variables={})

    self.assertEquals(suite.name, "fake_testsuite")
    self.assertEquals(suite.test_config, self.test_config)

    # Verify that the components of the TestSuite is loaded.
    self.assertIsNotNone(suite.tests)
    self.assertIsNotNone(suite.statusfile)


if __name__ == '__main__':
    unittest.main()
