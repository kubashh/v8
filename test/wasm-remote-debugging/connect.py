# Copyright 2019 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Flags: -expose-wasm --wasm_gdb_remote --wasm-pause-waiting-for-debugger --wasm-interpret-all sort_test.js

import os
import subprocess
import unittest
import sys
import gdb_rsp

# These are set up by Main().
SEL_LDR_COMMAND = None


class Tests(unittest.TestCase):
  def test_disconnect(self):
    sel_ldr = gdb_rsp.PopenDebugStub(SEL_LDR_COMMAND)
    try:
      # Connect and record the instruction pointer.
      connection = gdb_rsp.GdbRspConnection()
      connection.Close()
      # Reconnect 3 times.
      for _ in range(3):
        connection = gdb_rsp.GdbRspConnection()
        connection.Close()
    finally:
      gdb_rsp.KillProcess(sel_ldr)


def Main():
  index = sys.argv.index('--')
  args = sys.argv[index + 1:]
  # The remaining arguments go to unittest.main().
  sys.argv = sys.argv[:index]
  global SEL_LDR_COMMAND
  SEL_LDR_COMMAND = args
  os.chdir(os.path.join(os.path.dirname(__file__), 'test_files'))
  unittest.main()

if __name__ == '__main__':
  Main()
