# Copyright 2017 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from testrunner.local import testsuite
from testrunner.objects import testcase


class TestSuite(testsuite.TestSuite):
  def ListTests(self, context):
    return map(self._create_test, ['bananas', 'strawberries'])

  def _test_class(self):
    return TestCase


class TestCase(testcase.TestCase):
  def _get_shell(self):
    return 'd8_mocked.py'

  def _get_files_params(self, ctx):
    return [self.name]

def GetSuite(name, root):
  return TestSuite(name, root)
