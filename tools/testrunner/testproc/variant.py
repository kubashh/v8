# Copyright 2018 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import collections

from . import base
from ..local.variants import ALL_VARIANTS, ALL_VARIANT_FLAGS, FAST_VARIANT_FLAGS
from .result import GroupedResult


FAST_VARIANTS = set(["default", "turbofan"])
STANDARD_VARIANT = set(["default"])


class VariantProc(base.TestProcProducer):
  """Processor creating variants.

  For each test it keeps generator that returns variant, flags and id suffix.
  It produces variants one at a time, so it's waiting for the result of one
  variant to create another variant of the same test.
  It maintains the order of the variants passed to the init.

  There are some cases when particular variant of the test is not valid. To
  ignore subtests like that StatusFileFilterProc should be placed somewhere
  after the VariantProc.
  """

  def __init__(self, variants):
    super(VariantProc, self).__init__('VariantProc')
    self._next_test_iter = {}
    self._results = collections.defaultdict(list)
    self._variant_gens = {}
    self._variants = variants

  def _next_test(self, test):
    self._init_test(test)
    self._try_send_new_subtest(test)

  def _result_for(self, test, subtest, result):
    self._results[test.procid].append((subtest, result))
    self._try_send_new_subtest(test)

  def _init_test(self, test):
    self._next_test_iter[test.procid] = iter(self._variants_gen(test))

  def _try_send_new_subtest(self, test):
    # Sends result to the previous processor after it finished.
    try:
      variant, flags, suffix = next(self._next_test_iter[test.procid])
      subtest = self._create_subtest(test, '%s-%s' % (variant, suffix),
                                     variant=variant, flags=flags)
      self._send_test(subtest)
    except StopIteration:
      del self._next_test_iter[test.procid]
      # TODO(majeski): Don't group tests if previous processors don't need them.
      result = GroupedResult.create(self._results[test.procid])
      del self._results[test.procid]
      self._send_result(test, result)


  def _variants_gen(self, test):
    """Generator producing (variant, flags, procid suffix) tuples."""
    return self._get_variants_gen(test).gen(test)

  def _get_variants_gen(self, test):
    key = test.suite.name
    variants_gen = self._variant_gens.get(key, None)
    if variants_gen is None:
      variants_gen = test.suite.get_variants_gen(self._variants)
      self._variant_gens[key] = variants_gen
    return variants_gen
