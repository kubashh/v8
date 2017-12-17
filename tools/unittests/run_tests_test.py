#!/usr/bin/env python
# Copyright 2017 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import collections
import contextlib
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

from cStringIO import StringIO

TOOLS_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TEST_DATA_ROOT = os.path.join(TOOLS_ROOT, 'unittests', 'testdata')
RUN_TESTS_PY = os.path.join(TOOLS_ROOT, 'run-tests.py')

Result = collections.namedtuple(
    'Result', ['stdout', 'stderr', 'returncode'])

Result.__str__ = lambda self: (
    '\nReturncode: %s\nStdout:\n%s\nStderr:%s\n' %
    (self.returncode, self.stdout, self.stderr))


@contextlib.contextmanager
def temp_dir():
  path = None
  try:
    path = tempfile.mkdtemp('v8_test_')
    yield path
  finally:
    if path:
      shutil.rmtree(path)


@contextlib.contextmanager
def temp_base(baseroot):
  basedir = os.path.join(TEST_DATA_ROOT, baseroot)
  with temp_dir() as tempbase:
    outdir = os.path.join(tempbase, 'out')
    builddir = os.path.join(tempbase, 'out', 'Release')
    testroot = os.path.join(tempbase, 'test')
    os.makedirs(outdir)
    os.makedirs(builddir)
    os.makedirs(testroot)
    shutil.copy(os.path.join(basedir, 'v8_build_config.json'), builddir)
    shutil.copy(os.path.join(basedir, 'd8_mocked.py'), builddir)

    for suite in os.listdir(os.path.join(basedir, 'test')):
      os.makedirs(os.path.join(testroot, suite))
      for entry in os.listdir(os.path.join(basedir, 'test', suite)):
        shutil.copy(
            os.path.join(basedir, 'test', suite, entry),
            os.path.join(testroot, suite))
    yield tempbase


@contextlib.contextmanager
def capture():
  oldout = sys.stdout
  olderr = sys.stderr
  try:
    stdout=StringIO()
    stderr=StringIO()
    sys.stdout = stdout
    sys.stderr = stderr
    yield stdout, stderr
  finally:
    sys.stdout = oldout
    sys.stderr = olderr


def run_tests(basedir, *args):
  with capture() as (stdout, stderr):
    sys_args = ['--command-prefix', sys.executable] + list(args)
    code = standard_runner.StandardTestRunner(basedir=basedir).execute(sys_args)
    return Result(stdout.getvalue(), stderr.getvalue(), code)


class SystemTest(unittest.TestCase):
  @classmethod
  def setUpClass(cls):
    import coverage
    cls._cov = coverage.Coverage(
        source=([os.path.join(TOOLS_ROOT, 'testrunner')]),
        omit=['*unittest*', '*__init__.py'],
        concurrency='multiprocessing',
    )
    cls._cov.start()
    sys.path.append(TOOLS_ROOT)
    from testrunner import standard_runner
    global standard_runner

  @classmethod
  def tearDownClass(cls):
    cls._cov.stop()
    cls._cov.combine()
    print ''
    print cls._cov.report()

  def testPass(self):
    with temp_base('testroot1') as basedir:
      result = run_tests(
          basedir,
          '--mode=Release',
          '--progress=verbose',
          '--variants=default,stress',
          'sweet/bananas',
      )
      self.assertIn('Running 2 tests', result.stdout, result)
      self.assertIn('Done running sweet/bananas: pass', result.stdout, result)
      self.assertEqual(0, result.returncode, result)

  def testFail(self):
    with temp_base('testroot1') as basedir:
      result = run_tests(
          basedir,
          '--mode=Release',
          '--progress=verbose',
          '--variants=default,stress',
          'sweet/strawberries',
      )
      self.assertIn('Running 2 tests', result.stdout, result)
      self.assertIn('Done running sweet/strawberries: FAIL', result.stdout, result)
      self.assertEqual(1, result.returncode, result)


if __name__ == '__main__':
  unittest.main()
