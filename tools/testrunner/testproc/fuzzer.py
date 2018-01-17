# Copyright 2018 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from . import base


class FuzzerProc(base.TestProcProducer):
  def __init__(self, rng, count, is_optional, name):
    super(FuzzerProc, self).__init__(name)

    self._count = count
    self._rng = rng
    self._is_optional = is_optional

    self._is_analysis = False

  def start_analysis(self):
    self._is_analysis = True

  def finish_analysis(self):
    self._is_analysis = False

  def setup(self, requirement=base.DROP_RESULT):
    # Fuzzers are optimized to not store the results
    assert requirement == base.DROP_RESULT
    super(FuzzerProc, self).setup(requirement)

  def _next_test(self, test):
    raise NotImplementedError()

  def _result_for(self, test, subtest, result):
    raise NotImplementedError()

  def _create_subtest_with_flag(self, test, subtest_id, flag, value):
    flag_str = '%s=%d' % (flag, value)
    return self._create_subtest(test, subtest_id, flags=[flag_str])


class TwoStepFuzzerProc(FuzzerProc):
  def __init__(self, rng, count, is_optional, name):
    super(TwoStepFuzzerProc, self).__init__(rng, count, is_optional, name)

    self._gens = {}
    self._analysis_data = {}
    self._tests_to_skip = set()

  def _next_test(self, test):
    if self._is_analysis:
      subtest = self._create_analysis_subtest(test)
      self._send_test(subtest)
      return

    if test.procid in self._tests_to_skip:
      self._send_result(test, None)

    # TODO(majeski): should we make an analysis for each subtest if flags are
    # combined or just for the base test?
    base_test = test
    while base_test.origin:
      base_test = base_test.origin
    analysis_result = self._analysis_data[base_test.procid]
    self._gens[test.procid] = self._create_gen(test, analysis_result)
    self._try_send_next_test(test)

  def _create_analysis_subtest(self, test):
    return self._create_subtest(test, 'analysis',
                                flags=self._get_analysis_flags(),
                                keep_output=True)

  def _get_analysis_flags(self):
    raise NotImplementedError()

  def _result_for(self, test, subtest, result):
    if self._is_analysis:
      if result.has_unexpected_output:
        self._tests_to_skip(test.procid)
      else:
        base_test = test
        while base_test.origin:
          base_test = base_test.origin
        self._analysis_data[base_test.procid] = self._get_analysis_data(result)
      self._send_result(test, result)
    else:
      self._try_send_next_test(test)

  def _get_analysis_data(self, result):
    raise NotImplementedError()

  def _create_gen(self, test, analysis_result):
    raise NotImplementedError()

  def _try_send_next_test(self, test):
    for subtest in self._gens[test.procid]:
      print 'sending test %s from %s' % (subtest.procid, self._name)
      self._send_test(subtest)
      return
    del self._gens[test.procid]
    self._send_result(test, None)


class MarkingFuzzerProc(TwoStepFuzzerProc):
  def __init__(self, rng, count, is_optional):
    super(MarkingFuzzerProc, self).__init__(rng, count, is_optional,
                                            'MarkingFuzzer')

  def _get_analysis_flags(self):
    return ['--fuzzer-gc-analysis']

  def _get_analysis_data(self, result):
    for line in reversed(result.output.stdout.splitlines()):
      if line.startswith('### Maximum new space size reached = '):
        return int(float(line.split()[7]))

  def _create_gen(self, test, max_limit):
    if max_limit:
      values = self._rng.sample(xrange(1, max_limit + 1),
                                min(max_limit, self._count))
    else:
      values = []
    print test.procid, max_limit, values

    if self._is_optional:
      yield self._create_subtest(test, '0')
    for n, value in enumerate(values):
      yield self._create_subtest_with_flag(test, str(n + 1),
                                           '--stress-marking', value)


class CompactionFuzzerProc(FuzzerProc):
  def __init__(self, rng, is_optional):
    super(CompactionFuzzerProc, self).__init__(rng, 1, is_optional,
                                               'CompactionFuzzer')
    self._gens = {}

  def _next_test(self, test):
    if self._is_analysis:
      self._send_test(test)
      return

    self._gens[test.procid] = self._create_gen(test)
    self._try_send_next_test(test)

  def _result_for(self, test, subtest, result):
    if self._is_analysis:
      self._send_result(self, subtest)
      return

    self._try_send_next_test(test)

  def _create_gen(self, test):
    if self._is_optional:
      yield self._create_subtest(test, '0')
    yield self._create_subtest(test, '1', flags=['--stress-compaction-random'])

  def _try_send_next_test(self, test):
    for subtest in self._gens[test.procid]:
      print 'sending test %s from %s' % (subtest.procid, self._name)
      self._send_test(subtest)
      return
    del self._gens[test.procid]
    self._send_result(test, None)
