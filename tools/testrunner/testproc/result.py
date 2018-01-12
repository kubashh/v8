# Copyright 2018 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.


class ResultBase(object):
  @property
  def is_dummy(self):
    return False

  @property
  def is_grouped(self):
    return False


class Result(ResultBase):
  """Result created by the output processor."""

  def __init__(self, has_unexpected_output, output):
    self.has_unexpected_output = has_unexpected_output
    self.output = output


class GroupedResult(ResultBase):
  """Result consisting of multiple results. It can be used be processors that
  create multiple subtests for each test and want to pass all results back.
  """

  @staticmethod
  def create(results):
    """Create grouped result from the list of results. It filters out dummy
    results. If all results are dummy results it returns dummy result. If there
    is only one test return it.

    Args:
      results: list of pairs (test, result)
    """
    results = [(t, r) for (t, r) in results if not r.is_dummy]
    if not results:
      return DUMMY_RESULT
    if len(results) == 1:
      return results[0]
    return GroupedResult(results)

  def __init__(self, results):
    self.results = results

  @property
  def is_grouped(self):
    return True


class DummyResult(ResultBase):
  """Result without any meaningul value. Used primarily to inform the test
  processor that it's test wasn't executed.
  """

  @property
  def is_dummy(self):
    return True


DUMMY_RESULT = DummyResult()
