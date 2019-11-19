#!/usr/bin/python
# Copyright 2019 the V8 project authors. All rights reserved.

import subprocess
import sys
import unittest

import gdb_rsp


# These are set up by Main().
SEL_LDR_COMMAND = None


def PopenDebugStub():
  gdb_rsp.EnsurePortIsAvailable()
  return subprocess.Popen(SEL_LDR_COMMAND)


def KillProcess(process):
  if process.returncode is not None:
    # kill() won't work if we've already wait()'ed on the process.
    return
  try:
    process.kill()
  except OSError:
    if sys.platform == 'win32':
      # If process is already terminated, kill() throws
      # "WindowsError: [Error 5] Access is denied" on Windows.
      pass
    else:
      raise
  process.wait()


class DebugStubTest(unittest.TestCase):

  def test_disconnect(self):
    print('### test_disconnect')
    sel_ldr = PopenDebugStub()
    try:
      # Connect and record the instruction pointer.
      connection = gdb_rsp.GdbRspConnection()
      connection.Close()
      # Reconnect 3 times.
      for _ in range(3):
        connection = gdb_rsp.GdbRspConnection()
        connection.Close()
    finally:
      KillProcess(sel_ldr)


def Main():
  index = sys.argv.index('--')
  args = sys.argv[index + 1:]
  # The remaining arguments go to unittest.main().
  sys.argv = sys.argv[:index]
  global SEL_LDR_COMMAND
  SEL_LDR_COMMAND = args
  unittest.main()


if __name__ == '__main__':
  Main()
