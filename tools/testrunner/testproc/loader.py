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
    self.loaded_tests = set()

  def _load_test(self):
    try:
      is_loaded = False
      while not is_loaded:
        test = next(self.tests)
        if test.procid in self.loaded_tests:
          continue

        is_loaded = self._send_test(test)
        if not is_loaded:
          continue

        self.loaded_tests.add(test.procid)

    except StopIteration:
      # No more tests to load.
      return False

    return True

  def load_initial_tests(self, initial_batch_size):
    """
    Args:
      exec_proc: execution processor that the tests are being loaded into
      initial_batch_size: initial number of tests to load
    """
    for _ in range(initial_batch_size):
      if not self._load_test():
        # Ran out of tests
        return

  def next_test(self, test):
    assert False, 'Nothing can be connected to the LoadProc'

  def result_for(self, test, result):
    self._load_test()
