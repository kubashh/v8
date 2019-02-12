#!/usr/bin/env python
# Copyright 2019 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import collections
import contextlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

from cStringIO import StringIO

TOOLS_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TEST_DATA_ROOT = os.path.join(TOOLS_ROOT, 'unittests', 'testdata')
RUN_TESTS_PY = os.path.join(TOOLS_ROOT, 'run-num-fuzzer.py')

sys.path.append(TOOLS_ROOT)

from testrunner import num_fuzzer

Result = collections.namedtuple(
    'Result', ['stdout', 'stderr', 'returncode'])

Result.__str__ = lambda self: (
    '\nReturncode: %s\nStdout:\n%s\nStderr:\n%s\n' %
    (self.returncode, self.stdout, self.stderr))


@contextlib.contextmanager
def temp_dir():
  """Wrapper making a temporary directory available."""
  path = None
  try:
    path = tempfile.mkdtemp('v8_test_')
    yield path
  finally:
    if path:
      shutil.rmtree(path)


@contextlib.contextmanager
def temp_base(baseroot='testroot1'):
  """Wrapper that sets up a temporary V8 test root.

  Args:
    baseroot: The folder with the test root blueprint. Relevant files will be
        copied to the temporary test root, to guarantee a fresh setup with no
        dirty state.
  """
  basedir = os.path.join(TEST_DATA_ROOT, baseroot)
  with temp_dir() as tempbase:
    builddir = os.path.join(tempbase, 'out')
    testroot = os.path.join(tempbase, 'test')
    os.makedirs(builddir)
    os.makedirs(testroot)
    shutil.copy(os.path.join(basedir, 'v8_build_config.json'), builddir)
    shutil.copy(os.path.join(basedir, 'd8_mocked.py'), builddir)
    yield tempbase


@contextlib.contextmanager
def capture():
  """Wrapper that replaces system stdout/stderr an provides the streams."""
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


def run_fuzzer_tests(basedir, *args, **kwargs):
  """Executes the test runner with captured output."""
  with capture() as (stdout, stderr):
    sys_args = ['--command-prefix', sys.executable] + list(args)
    code = num_fuzzer.NumFuzzer(basedir=basedir).execute(sys_args)
    return Result(stdout.getvalue(), stderr.getvalue(), code)


class SystemTest(unittest.TestCase):
  @classmethod
  def setUpClass(cls):
    # Try to set up python coverage and run without it if not available.
    cls._cov = None
    try:
      import coverage
      if int(coverage.__version__.split('.')[0]) < 4:
        cls._cov = None
        print 'Python coverage version >= 4 required.'
        raise ImportError()
      cls._cov = coverage.Coverage(
          source=([os.path.join(TOOLS_ROOT, 'testrunner')]),
          omit=['*unittest*', '*__init__.py'],
      )
      cls._cov.exclude('raise NotImplementedError')
      cls._cov.exclude('if __name__ == .__main__.:')
      cls._cov.exclude('except TestRunnerError:')
      cls._cov.exclude('except KeyboardInterrupt:')
      cls._cov.exclude('if options.verbose:')
      cls._cov.exclude('if verbose:')
      cls._cov.exclude('pass')
      cls._cov.exclude('assert False')
      cls._cov.start()
    except ImportError:
      print 'Running without python coverage.'
    sys.path.append(TOOLS_ROOT)
    global standard_runner
    from testrunner import standard_runner
    from testrunner.local import command
    from testrunner.local import pool
    command.setup_testing()
    pool.setup_testing()

  @classmethod
  def tearDownClass(cls):
    if cls._cov:
      cls._cov.stop()
      print ''
      print cls._cov.report(show_missing=True)

  def testPass(self):
    """Test running only passing tests in two variants.

    Also test printing durations.
    """
    with temp_base() as basedir:
      result = run_fuzzer_tests(
          basedir,
      )

      self.assertEqual(0, result.returncode, result)


if __name__ == '__main__':
  unittest.main()
