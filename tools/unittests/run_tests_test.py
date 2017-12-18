#!/usr/bin/env python
# Copyright 2017 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""
Global system tests for V8 test runners and fuzzers.

This hooks up the framework under tools/testrunner testing high-level scenarios
with different test suite extensions and build configurations.
"""

# TODO(machenbach): Mock out util.GuessOS to make these tests really platform
# independent.
# TODO(machenbach): Move coverage recording to a global test entry point to
# include other unittest suites in the coverage report.
# TODO(majeski): Add some tests for the fuzzers.

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
RUN_TESTS_PY = os.path.join(TOOLS_ROOT, 'run-tests.py')

Result = collections.namedtuple(
    'Result', ['stdout', 'stderr', 'returncode'])

Result.__str__ = lambda self: (
    '\nReturncode: %s\nStdout:\n%s\nStderr:%s\n' %
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


def run_tests(basedir, *args):
  """Executes the test runner with captured output."""
  with capture() as (stdout, stderr):
    sys_args = ['--command-prefix', sys.executable] + list(args)
    code = standard_runner.StandardTestRunner(
        basedir=basedir).execute(sys_args)
    return Result(stdout.getvalue(), stderr.getvalue(), code)


def override_build_config(basedir, **kwargs):
  """Override the build config with new values provided as kwargs."""
  path = os.path.join(basedir, 'out', 'Release', 'v8_build_config.json')
  with open(path) as f:
    config = json.load(f)
    config.update(kwargs)
  with open(path, 'w') as f:
    json.dump(config, f)


class SystemTest(unittest.TestCase):
  @classmethod
  def setUpClass(cls):
    # Try to set up python coverage and run without it if not available.
    cls._cov = None
    try:
      import coverage
      if int(coverage.__version__.split('.')[0]) < 4:
        # First coverage 4.0 can deal with multiprocessing.
        cls._cov = None
        print 'Python coverage version >= 4 required.'
        raise ImportError()
      cls._cov = coverage.Coverage(
          source=([os.path.join(TOOLS_ROOT, 'testrunner')]),
          omit=['*unittest*', '*__init__.py'],
          concurrency='multiprocessing',
      )
      cls._cov.exclude('raise NotImplementedError')
      cls._cov.exclude('if __name__ == .__main__.:')
      cls._cov.exclude('except TestRunnerError:')
      cls._cov.exclude('except KeyboardInterrupt:')
      cls._cov.exclude('if options.verbose:')
      cls._cov.exclude('if verbose:')
      cls._cov.exclude('pass')
      cls._cov.start()
    except ImportError:
      print 'Running without python coverage.'
    sys.path.append(TOOLS_ROOT)
    from testrunner import standard_runner
    global standard_runner

  @classmethod
  def tearDownClass(cls):
    if cls._cov:
      cls._cov.stop()
      cls._cov.combine()
      print ''
      print cls._cov.report(show_missing=True)

  def testPass(self):
    """Test running only passing tests in two variants."""
    with temp_base() as basedir:
      result = run_tests(
          basedir,
          '--mode=Release',
          '--progress=verbose',
          '--variants=default,stress',
          'sweet/bananas',
          'sweet/raspberries',
      )
      self.assertIn('Running 4 tests', result.stdout, result)
      self.assertIn('Done running sweet/bananas: pass', result.stdout, result)
      self.assertEqual(0, result.returncode, result)

  def testFail(self):
    """Test running only failing tests in two variants."""
    with temp_base() as basedir:
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

  def testAutoDetect(self):
    """Fake a build with several auto-detected options.

    Using all those options at once doesn't really make much sense. This is
    merly for getting coverage.
    """
    with temp_base() as basedir:
      override_build_config(
          basedir, dcheck_always_on=True, is_asan=True, is_cfi=True,
          is_msan=True, is_tsan=True, is_ubsan_vptr=True, target_cpu='x86',
          v8_enable_i18n_support=False, v8_target_cpu='x86',
          v8_use_snapshot=False)
      result = run_tests(
          basedir,
          '--mode=Release',
          '--progress=verbose',
          '--variants=default',
          'sweet/bananas',
      )
      expect_text = """>>> Autodetected:
asan
cfi_vptr
dcheck_always_on
msan
no_i18n
no_snap
tsan
ubsan_vptr
>>> Running tests for ia32.release"""
      self.assertIn(expect_text, result.stdout, result)
      self.assertEqual(0, result.returncode, result)
      # TODO(machenbach): Test some more implications of the auto-detected
      # options, e.g. that the right env variables are set.

  def testSkips(self):
    """Test skipping tests in status file for a specific variant."""
    with temp_base() as basedir:
      result = run_tests(
          basedir,
          '--mode=Release',
          '--progress=verbose',
          '--variants=nooptimization',
          'sweet/strawberries',
      )
      self.assertIn('Running 0 tests', result.stdout, result)
      self.assertEqual(0, result.returncode, result)

  def testDefault(self):
    """Test using default test suites, though no tests are run since they don't
    exist in a test setting.
    """
    with temp_base() as basedir:
      result = run_tests(basedir, '--mode=Release')
      self.assertIn('Warning: no tests were run!', result.stdout, result)
      self.assertEqual(0, result.returncode, result)

  def testNoBuildConfig(self):
    """Test failing run when build config is not found."""
    with temp_base() as basedir:
      result = run_tests(basedir)
      self.assertIn('Failed to load build config', result.stdout, result)
      self.assertEqual(1, result.returncode, result)

  def testGNOption(self):
    """Test using gn option, but no gn build folder is found."""
    with temp_base() as basedir:
      # TODO(machenbach): This should fail gracefully.
      with self.assertRaises(OSError):
        run_tests(basedir, '--gn')

  def testInconsistentMode(self):
    """Test failing run when attempting to wrongly override the mode."""
    with temp_base() as basedir:
      override_build_config(basedir, is_debug=True)
      result = run_tests(basedir, '--mode=Release')
      self.assertIn('execution mode (release) for release is inconsistent '
                    'with build config (debug)', result.stdout, result)
      self.assertEqual(1, result.returncode, result)

  def testInconsistentArch(self):
    """Test failing run when attempting to wrongly override the arch."""
    with temp_base() as basedir:
      result = run_tests(basedir, '--mode=Release', '--arch=ia32')
      self.assertIn(
          '--arch value (ia32) inconsistent with build config (x64).',
          result.stdout, result)
      self.assertEqual(1, result.returncode, result)

  def testModeFromBuildConfig(self):
    """Test auto-detection of mode from build config."""
    with temp_base() as basedir:
      result = run_tests(basedir, '--outdir=out/Release', 'sweet/bananas')
      self.assertEqual(0, result.returncode, result)

  def testReport(self):
    """Test the report feature.

    This also exercises various paths in statusfile logic.
    """
    with temp_base() as basedir:
      override_build_config(basedir)
      result = run_tests(
          basedir,
          '--mode=Release',
          '--variants=default',
          'sweet',
          '--report',
      )
      self.assertIn(
          '3 tests are expected to fail that we should fix',
          result.stdout, result)
      self.assertEqual(1, result.returncode, result)

  def testPredictable(self):
    """Test running a test in verify-predictable mode.

    The test will fail because of missing allocation output. We verify that and
    that the predictable flags are passed and printed after failure.
    """
    with temp_base() as basedir:
      override_build_config(basedir, v8_enable_verify_predictable=True)
      result = run_tests(
          basedir,
          '--mode=Release',
          '--progress=verbose',
          '--variants=default',
          'sweet/bananas',
      )
      self.assertIn('Running 1 tests', result.stdout, result)
      self.assertIn('Done running sweet/bananas: FAIL', result.stdout, result)
      self.assertIn('Test had no allocation output', result.stdout, result)
      self.assertIn('--predictable --verify_predictable', result.stdout, result)
      self.assertEqual(1, result.returncode, result)

if __name__ == '__main__':
  unittest.main()
