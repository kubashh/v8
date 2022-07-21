# Copyright 2018 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Contains a helper function for deploying and executing a packaged
executable on a Target."""
from __future__ import print_function

import logging
import multiprocessing
import os
import select
import subprocess
import sys
import threading
import time

from contextlib import contextmanager

BASE_DIR = os.path.normpath(
    os.path.join(
        os.path.dirname(os.path.abspath(__file__)), '..', '..', '..', '..'))

FUCHSIA_DIR = os.path.join(BASE_DIR, 'build', 'fuchsia')
sys.path.insert(0, FUCHSIA_DIR)

import common
from exit_on_sig_term import ExitOnSigTerm
from symbolizer import BuildIdsPaths, RunSymbolizer
from fvdl_target import FvdlTarget

CreateFromArgs = FvdlTarget.CreateFromArgs
FAR = common.GetHostToolPathFromPlatform('far')
# Amount of time to wait for the termination of the system log output thread.
_JOIN_TIMEOUT_SECS = 5


class MergedInputStream(object):
  """Merges a number of input streams into a UTF-8 encoded UNIX pipe on a
  dedicated thread. Terminates when the file descriptor of the primary stream
  (the first in the sequence) is closed."""

  def __init__(self, streams):
    assert len(streams) > 0
    self._streams = streams
    self._output_stream = None
    self._thread = None

  def Start(self):
    """Returns a pipe to the merged output stream."""
    read_pipe, write_pipe = os.pipe()
    self._output_stream = os.fdopen(write_pipe, 'wb', 0)
    self._thread = threading.Thread(target=self._Run)
    self._thread.start()
    return os.fdopen(read_pipe, 'r')

  def _Run(self):
    streams_by_fd = {}
    primary_fd = self._streams[0].fileno()
    for s in self._streams:
      streams_by_fd[s.fileno()] = s
    # Set when the primary FD is closed. Input from other FDs will continue to
    # be processed until select() runs dry.
    flush = False
    # The lifetime of the MergedInputStream is bound to the lifetime of
    # |primary_fd|.
    while primary_fd:
      # When not flushing: block until data is read or an exception occurs.
      rlist, _, xlist = select.select(streams_by_fd, [], streams_by_fd)
      if len(rlist) == 0 and flush:
        break
      for fileno in xlist:
        del streams_by_fd[fileno]
        if fileno == primary_fd:
          primary_fd = None
      for fileno in rlist:
        line = streams_by_fd[fileno].readline()
        if line:
          self._output_stream.write(line)
        else:
          del streams_by_fd[fileno]
          if fileno == primary_fd:
            primary_fd = None
    # Flush the streams by executing nonblocking reads from the input file
    # descriptors until no more data is available,  or all the streams are
    # closed.
    while streams_by_fd:
      rlist, _, _ = select.select(streams_by_fd, [], [], 0)
      if not rlist:
        break
      for fileno in rlist:
        line = streams_by_fd[fileno].readline()
        if line:
          self._output_stream.write(line)
        else:
          del streams_by_fd[fileno]


def _GetComponentUri(package_name, package_component_version):
  suffix = 'cm' if package_component_version == '2' else 'cmx'
  return 'fuchsia-pkg://fuchsia.com/%s#meta/%s.%s' % (package_name,
                                                      package_name, suffix)


class RunTestPackageArgs:
  """RunTestPackage() configuration arguments structure.
  code_coverage: If set, the test package will be run via 'runtests', and the
                 output will be saved to /tmp folder on the device.
  test_realm_label: Specifies the realm name that run-test-component should use.
      This must be specified if a filter file is to be set, or a results summary
      file fetched after the test suite has run.
  use_run_test_component: If True then the test package will be run hermetically
                          via 'run-test-component', rather than using 'run'.
  output_directory: If set, the output directory for CFv2 tests that use
                    custom artifacts; see fxb/75690.
  """

  def __init__(self):
    self.code_coverage = False
    self.test_realm_label = None
    self.use_run_test_component = False
    self.output_directory = None

  @staticmethod
  def FromCommonArgs(args):
    run_test_package_args = RunTestPackageArgs()
    run_test_package_args.code_coverage = args.code_coverage
    return run_test_package_args


def _SymbolizeStream(input_fd, ids_txt_files):
  """Returns a Popen object for a symbolizer process invocation.
  input_fd: The data to symbolize.
  ids_txt_files: A list of ids.txt files which contain symbol data."""
  return RunSymbolizer(input_fd, subprocess.PIPE, ids_txt_files)


@contextmanager
def InstallTestPackage(target, package_paths):
  """Installs the Fuchsia package at |package_path| on the target,.
  target: The deployment Target object that will run the package.
  package_paths: The paths to the .far packages to be installed.
  """
  try:
    # Spin up a thread to asynchronously dump the system log to stdout
    # for easier diagnoses of early, pre-execution failures.
    log_output_quit_event = multiprocessing.Event()
    with ExitOnSigTerm(), target.GetPkgRepo():
      start_time = time.time()
      target.InstallPackage(package_paths)
      logging.info('Test installed in {:.2f} seconds.'.format(time.time() -
                                                              start_time))
      log_output_quit_event.set()
      yield
  finally:
    logging.info('Terminating kernel log reader.')
    log_output_quit_event.set()


class PrintStream():

  def print(self, text):
    print(text)


DEFAULT_PRINT_STREAM = PrintStream()


def RunTest(target,
            ffx_session,
            package_paths,
            package_name,
            package_component_version,
            package_args,
            args,
            print_stream=DEFAULT_PRINT_STREAM):
  logging.info('Running application.')
  component_uri = _GetComponentUri(package_name, package_component_version)
  process = None
  if ffx_session:
    process = ffx_session.test_run(target.GetFfxTarget(), component_uri,
                                   package_args)
  elif args.code_coverage:
    # TODO(crbug.com/1156768): Deprecate runtests.
    # runtests requires specifying an output directory and a double dash
    # before the argument list.
    command = ['runtests', '-o', '/tmp', component_uri]
    if args.test_realm_label:
      command += ['--realm-label', args.test_realm_label]
    command += ['--']
    command.extend(package_args)
  elif args.use_run_test_component:
    command = ['run-test-component']
    if args.test_realm_label:
      command += ['--realm-label=%s' % args.test_realm_label]
    command.append(component_uri)
    command.append('--')
    command.extend(package_args)
  else:
    command = ['run', component_uri]
    command.extend(package_args)
  if process is None:
    process = target.RunCommandPiped(
        command,
        stdin=open(os.devnull, 'r'),
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT)
  # Symbolize klog and systemlog as separate streams. The symbolizer
  # protocol is stateful, so comingled raw stack dumps can yield
  # unsymbolizable garbage data.
  ids_txt_paths = BuildIdsPaths(package_paths)
  with _SymbolizeStream(process.stdout, ids_txt_paths) as \
          symbolized_stdout:
    output_stream = MergedInputStream([symbolized_stdout.stdout]).Start()
    for next_line in output_stream:
      print_stream.print(next_line.rstrip())
    symbolized_stdout.wait()  # Should return instantly.
  process.wait()
  if process.returncode == 0:
    logging.info('Process exited normally with status code 0.')
  else:
    # The test runner returns an error status code if *any* tests fail,
    # so we should proceed anyway.
    logging.warning('Process exited with status code %d.' % process.returncode)
  return process.returncode
