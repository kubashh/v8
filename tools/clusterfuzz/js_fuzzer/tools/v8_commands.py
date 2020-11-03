# Copyright 2016 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
# Fork from commands.py and output.py in v8 test driver.
import os
import signal
import subprocess
import sys
from threading import Event, Timer
PYTHON3 = sys.version_info >= (3, 0)
class Command(object):
  """Represents a configuration for running V8 multiple times with certain
  flags and files.
  """
  def __init__(self, executable, flags):
    self.executable = executable
    self.flags = flags
  def run(self, testcase, timeout):
    """Run the executable with a specific testcase."""
    args = [self.executable] + self.flags + [testcase]
    print(' '.join(args))
    return Execute(
        args,
        cwd=os.path.dirname(os.path.abspath(testcase)),
        timeout=timeout,
    )
class Output(object):
  def __init__(self, exit_code, timeout, stdout, pid):
    self.exit_code = exit_code
    self.timeout = timeout
    self.stdout = stdout
    self.pid = pid
  def HasCrashed(self):
    return (self.exit_code < 0 and
            self.exit_code != -signal.SIGABRT)
def Execute(args, cwd, timeout=None):
  popen_args = [c for c in args if c != ""]
  os.environ['ASAN_OPTIONS'] = "alloc_dealloc_mismatch=0:allocator_may_return_null=1:allow_user_segv_handler=1:check_malloc_usable_size=0:detect_leaks=1:detect_odr_violation=0:detect_stack_use_after_return=1:fast_unwind_on_fatal=1:handle_abort=1:handle_segv=1:handle_sigbus=1:handle_sigfpe=1:handle_sigill=1:handle_sigtrap=1:max_uar_stack_size_log=16:print_scariness=1:print_summary=1:print_suppressions=0:redzone=32:strict_memcmp=0:symbolize=1:symbolize_inline_frames=false:use_sigaltstack=1"
  kwargs = {}
  if PYTHON3:
    kwargs['encoding'] = 'utf-8'
  try:
    process = subprocess.Popen(
      args=popen_args,
      stdout=subprocess.PIPE,
      stderr=subprocess.STDOUT,
      cwd=cwd,
      **kwargs
    )
  except Exception as e:
    sys.stderr.write("Error executing: %s\n" % popen_args)
    raise e
  timeout_event = Event()
  def kill_process():
    timeout_event.set()
    try:
      process.kill()
    except OSError:
      sys.stderr.write('Error: Process %s already ended.\n' % process.pid)
  timer = Timer(timeout, kill_process)
  timer.start()
  stdout, _ = process.communicate()
  timer.cancel()
  return Output(
      process.returncode,
      timeout_event.is_set(),
      stdout,
      process.pid,
  )
