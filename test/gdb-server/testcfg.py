# Copyright 2017 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os

from testrunner.local import testsuite, command
from testrunner.objects import testcase

class TestLoader(testsuite.PythonTestLoader):
  def _list_test_filenames(self):
    return ['debug_stub_test.py']

class TestSuite(testsuite.TestSuite):
  def __init__(self, *args, **kwargs):
    super(TestSuite, self).__init__(*args, **kwargs)
    self.test_root = os.path.join(self.root, "")
    self._test_loader.test_root = self.test_root

  def _test_loader_class(self):
    return TestLoader

  def _test_class(self):
    return TestCase

class TestCase(testcase.PythonTestCase):
  def __init__(self, suite, path, name, test_config):
    super(testcase.PythonTestCase, self).__init__(suite, path, name, test_config)
    os.chdir(suite.test_root + '\\sort')

  def _get_files_params(self):
    return [os.path.join(self.suite.test_root, self.path + self._get_suffix())]

  def _get_random_seed_flags(self):
    return []

  def _get_mode_flags(self):
    return ['--', self._test_config.shell_dir + '\\d8', '-expose-wasm',
            '--wasm_gdb_remote', '--wasm-pause-waiting-for-debugger',
            '--wasm-interpret-all', 'sort_test.js']

  def _create_cmd(self, shell, params, env, timeout):
    return command.Command(
      cmd_prefix=self._test_config.command_prefix,
      shell=shell,
      args=params,
      env=env,
      timeout=timeout,
      verbose=self._test_config.verbose,
      resources_func=self._get_resources,
    )

def GetSuite(*args, **kwargs):
  return TestSuite(*args, **kwargs)
