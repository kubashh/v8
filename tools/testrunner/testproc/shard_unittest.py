#!/usr/bin/env python
# Copyright 2019 the V8 project authors. All rights reserved.
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

from testrunner.testproc.shard import radix_hash


class TestRadixHashing(unittest.TestCase):
  def test_hash_character_by_radix(self):
    self.assertEqual(97, radix_hash(capacity=2**32, key="a"))

  def test_hash_character_by_radix_with_capacity(self):
    self.assertEqual(6, radix_hash(capacity=7, key="a"))

  def test_hash_string(self):
    self.assertEqual(6, radix_hash(capacity=7, key="ab"))

  def test_hash_test_id(self):
    self.assertEqual(
      5,
      radix_hash(capacity=7,
                 key="test262/Map/class-private-method-Variant-0-1"))

if __name__ == '__main__':
  unittest.main()
