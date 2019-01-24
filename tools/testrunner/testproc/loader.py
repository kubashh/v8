# Copyright 2018 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from . import base


class LoadProc(base.TestProc):
  """First processor in the chain that passes all tests to the next processor.
  """

  def __init__(self, tests):
    super(LoadProc, self).__init__()

    self.tests = tests

    self._test_id = 0

  def _assign_test_id(self, test):
      test.set_id(self._test_id)
      self._test_id += 1

  def load_initial_tests(self, exec_proc, initial_batch_size):
    """
    Args:
      exec_proc: execution processor that the tests are being loaded into
      initial_batch_size: initial number of tests to load
    """
    while exec_proc.loaded_tests < initial_batch_size:
      try:
        test = next(self.tests)
      except StopIteration:
        return

      self._assign_test_id(test)
      self._send_test(test)

  def next_test(self, test):
    assert False, 'Nothing can be connected to the LoadProc'

  def result_for(self, test, result):
    try:
      test = next(self.tests)
    except StopIteration:
      # No more tests to load.
      return

    self._assign_test_id(test)
    self._send_test(test)
