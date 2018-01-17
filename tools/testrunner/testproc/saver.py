# Copyright 2018 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from . import base


class SaveProc(base.TestProc):
  """Last processor in the chain that saves all tests that it received."""

  def __init__(self):
    super(SaveProc, self).__init__()
    self.tests = []

  def connect_to(self, next_proc):
    assert False, 'SaveProc cannot be connected to anything'

  def next_test(self, test):
    self.tests.append(test)

  def result_for(self, test, result):
    assert False, 'SaveProc cannot receive results'
