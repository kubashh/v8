

# Building:
# cat out/fuchsia/args.gn 
# dcheck_always_on = false
# is_component_build = false
# is_debug = false
# target_cpu = "x64"
# target_os = "fuchsia"
# use_goma = true
# v8_enable_google_benchmark = true

# Running on laptop with 8 cores:
# time python3 fuchsia.py
# 4m21.780s
# Using 1 process results in ~10 minutes.

import logging
import multiprocessing
import os
import subprocess
import sys


BASE_DIR = os.path.dirname(os.path.abspath(__file__))
FUCHSIA_DIR = os.path.join(BASE_DIR, 'build', 'fuchsia')
BUILD_DIR = os.path.join(BASE_DIR, 'out', 'fuchsia')

UNITTESTS_PACKAGE_PATH = 'gen/test/unittests/v8_unittests/v8_unittests.far'


DISABLED_TEST_PREFIXES = [
  # Errors due to some file dependency?
  'BytecodeGeneratorTest.',
  # Hangs.
  'CodePagesTest.LargeCodeObjectWithSignalHandler',
]


# C/P from blinkpy/web_tests/port/fuchsia.py - edits are marked with EDIT.
class _TargetHost(object):
    def __init__(self, build_path, build_ids_path, ports_to_forward, target,
                 results_directory):
        try:
            self._pkg_repo = None
            self._target = target
            self._target.Start()
            self._setup_target(build_path, build_ids_path, ports_to_forward,
                               results_directory)
        except:
            self.cleanup()
            raise

    def _setup_target(self, build_path, build_ids_path, ports_to_forward,
                      results_directory):
        # Tell SSH to forward all server ports from the Fuchsia device to
        # the host.
        forwarding_flags = [
            '-O',
            'forward',  # Send SSH mux control signal.
            '-N',  # Don't execute command
            '-T'  # Don't allocate terminal.
        ]
        for port in ports_to_forward:
            forwarding_flags += ['-R', '%d:localhost:%d' % (port, port)]
        self._proxy = self._target.RunCommandPiped([],
                                                   ssh_args=forwarding_flags,
                                                   stdout=subprocess.PIPE,
                                                   stderr=subprocess.STDOUT)

        # EDIT: Use UNITTESTS_PACKAGE_PATH.
        package_path = os.path.join(build_path, UNITTESTS_PACKAGE_PATH)
        self._target.StartSystemLog([package_path])

        self._pkg_repo = self._target.GetPkgRepo()
        self._pkg_repo.__enter__()

        self._target.InstallPackage([package_path])

        # Process will be forked for each worker, which may make QemuTarget
        # unusable (e.g. waitpid() for qemu process returns ECHILD after
        # fork() ). Save command runner before fork()ing, to use it later to
        # connect to the target.
        self.target_command_runner = self._target.GetCommandRunner()

    # EDIT: Don't use stdin. Pipe stderr to stdout.
    def run_command(self, command):
        return self.target_command_runner.RunCommandPiped(
            command,
            #stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT)

    def cleanup(self):
        try:
            if self._pkg_repo:
                self._pkg_repo.__exit__(None, None, None)
        finally:
            if self._target:
                self._target.Stop()


# Main process will set this up.
target_host = None


def run_command(*args):
  print(' '.join(args))
  cmd = [
    'run',
    'fuchsia-pkg://fuchsia.com/v8_unittests#meta/v8_unittests.cmx',
  ] + list(args)
  process = target_host.run_command(cmd)
  stdout, _ = process.communicate()
  return stdout.decode('utf-8')


def run_test(name):
  return run_command(
      '--gtest_random_seed=123', '--gtest_print_time=0',
      f'--gtest_filter={name}')


if __name__ == '__main__':
  sys.path.insert(0, FUCHSIA_DIR)
  from common_args import (
      _GetPathToBuiltinTarget, _LoadTargetClass, InitializeTargetArgs)

  # C/P from blinkpy/web_tests/port/fuchsia.py with simplified args.
  target_args = InitializeTargetArgs()
  target_args.out_dir = BUILD_DIR
  target_args.target_cpu = 'x64'
  target_args.fuchsia_out_dir = None
  target_args.ssh_config = None
  target_args.host = None
  target_args.port = None
  target_args.node_name = None
  target_args.cpu_cores = 8
  target_args.logs_dir = None

  # TODO: 'fvdl' hangs in fvdl_target.py", line 184, in _ConnectToTarget
  target = _LoadTargetClass(
      _GetPathToBuiltinTarget('qemu')).CreateFromArgs(target_args)

  # C/P from blinkpy/web_tests/port/fuchsia.py with UNITTESTS_PACKAGE_PATH
  package_path = os.path.join(BUILD_DIR, UNITTESTS_PACKAGE_PATH)
  build_ids_path = os.path.join(os.path.dirname(package_path), 'ids.txt')
  SERVER_PORTS = [8000, 8001, 8080, 8081, 8443, 8444, 8445, 8880, 9001, 9444]
  target_host = _TargetHost(BUILD_DIR,
                            build_ids_path,
                            SERVER_PORTS, target,
                            None)

  try:
    test_list = run_command('--gtest_list_tests')

    # C/P from test/unittests/testcfg.py
    test_names = []
    test_case = ''
    for line in test_list.splitlines():
      space = line.startswith(' ')
      test_desc = line.strip().split()[0]
      if test_desc.endswith('.'):
        test_case = test_desc
      elif test_case and test_desc and space:
        test_names.append(test_case + test_desc)

    # Disable some tests.
    def not_skipped(test):
      return not any(test.startswith(prefix) for prefix in DISABLED_TEST_PREFIXES)
    test_names = list(filter(not_skipped, test_names))

    # Use fork to reuse state of the target_host.
    ctx = multiprocessing.get_context('fork')

    # 4 workers seems to be an adequate number for 8 cores.
    with ctx.Pool(processes=4) as pool:
      for result in pool.imap_unordered(run_test, test_names):
        print(result)

  finally:
    target_host.cleanup()
