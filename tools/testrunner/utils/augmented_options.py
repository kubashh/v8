# Copyright 2022 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import random
import optparse

class AugmentedOptions(optparse.Values):
  @staticmethod
  def augment(options_object):
    options_object.__class__ = AugmentedOptions
    return options_object

  def fuzzer_rng(self):
    if not getattr(self,'_fuzzer_rng', None):
      self._fuzzer_rng = random.Random(self.fuzzer_random_seed)
    return self._fuzzer_rng
