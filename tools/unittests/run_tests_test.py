#!/usr/bin/env python
# Copyright 2017 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import shutil
import subprocess
import sys
import unittest

TOOLS_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TEST_DATA_ROOT = os.path.join(TOOLS_ROOT, 'unittests', 'testdata')
RUN_TESTS_PY = os.path.join(TOOLS_ROOT, 'run-tests.py')

def run_tests(testroot, *args):
  basedir = os.path.join(TEST_DATA_ROOT, 'testroot1')

  # Setup build dir.
  outdir = os.path.join(basedir, 'out')
  builddir = os.path.join(basedir, 'out', 'Release')
  if not os.path.isdir(outdir):
    os.makedirs(outdir)
  if not os.path.isdir(builddir):
    os.makedirs(builddir)
  shutil.copy(os.path.join(basedir, 'v8_build_config.json'), builddir)
  shutil.copy(os.path.join(basedir, 'd8_mocked.py'), builddir)

  # Run test driver.
  p = subprocess.Popen(
      [
        RUN_TESTS_PY,
        '--basedir', basedir,
        '--command-prefix', sys.executable,
        '--no-sorting',
      ] + list(args),
      stdout=subprocess.PIPE,
      stderr=subprocess.PIPE,
  )
  stdout, stderr = p.communicate()
  return stdout, stderr, p.returncode


class PerfTest(unittest.TestCase):
  def testBasic(self):
    stdout, stderr, code = run_tests(
        'testroot1',
        '--mode=Release',
        '--progress=verbose',
        '--variants=default,stress',
        'sweet',
    )
    self.assertIn('Running 2 tests', stdout)
    self.assertIn('Done running sweet/bananas: pass', stdout)
    self.assertEqual(0, code)


if __name__ == '__main__':
  unittest.main()
