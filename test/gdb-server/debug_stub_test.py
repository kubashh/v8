#!/usr/bin/python
# Copyright 2019 the V8 project authors. All rights reserved.

import array
import operator
import os
import re
import struct
import subprocess
import sys
import unittest
import xml.etree.ElementTree

import gdb_rsp

RETURNCODE_KILL = -9

NACL_SIGTRAP = 5

BREAK_ADDRESS_0 = 0x500002864 # raytracer.cpp line 267
BREAK_ADDRESS_1 = 0x500003501
BREAK_ADDRESS_2 = 0x500003d87

# These are set up by Main().
ARCH = 'wasm32'
NM_TOOL = None
SEL_LDR_COMMAND = None


def AssertEquals(x, y):
  if x != y:
    raise AssertionError('%r != %r' % (x, y))

def DecodeHex(data):
  assert len(data) % 2 == 0, data
  return ''.join([chr(int(data[index * 2 : (index + 1) * 2], 16))
                  for index in xrange(len(data) / 2)])

def DecodeUInt64Array(data):
  assert len(data) % 16 == 0, data
  result = []
  for index in xrange(len(data) / 16):
    value = 0
    for digit in xrange(7, -1, -1):
      value = value * 256 + int(data[index * 16 + digit * 2 : index * 16 + (digit + 1) * 2], 16)
    result.append(value)
  return result


def EncodeHex(data):
  return ''.join('%02x' % ord(byte) for byte in data)


def DecodeEscaping(data):
  ret = ''
  last = None
  repeat = False
  escape = False
  for byte in data:
    if escape:
      last = chr(ord(byte) ^ 0x20)
      ret += last
      escape = False
    elif repeat:
      count = ord(byte) - 29
      assert count >= 3 and count <= 97
      assert byte != '$' and byte != '#'
      ret += last * count
      repeat = False
    elif byte == '}':
      escape = True
    elif byte == '*':
      assert last is not None
      repeat = True
    else:
      ret += byte
      last = byte
  return ret


WASM32_REG_DEFS = [
    ('pc', 'Q'),
]

REG_DEFS = {
    'wasm32': WASM32_REG_DEFS,
    }


def DecodeRegs(reply):
  defs = REG_DEFS[ARCH]
  names = [reg_name for reg_name, reg_fmt in defs]
  fmt = ''.join([reg_fmt for reg_name, reg_fmt in defs])

  values = struct.unpack_from(fmt, DecodeHex(reply))
  return dict(zip(names, values))


def EncodeRegs(regs):
  defs = REG_DEFS[ARCH]
  names = [reg_name for reg_name, reg_fmt in defs]
  fmt = ''.join([reg_fmt for reg_name, reg_fmt in defs])

  values = [regs[r] for r in names]
  return EncodeHex(struct.pack(fmt, *values))


def PopenDebugStub():
  gdb_rsp.EnsurePortIsAvailable()
  return subprocess.Popen(SEL_LDR_COMMAND + ['-g', "test"])


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


class LaunchDebugStub(object):

  def __init__(self):
    self._proc = PopenDebugStub()

  def __enter__(self):
    try:
      return gdb_rsp.GdbRspConnection()
    except:
      KillProcess(self._proc)
      raise

  def __exit__(self, exc_type, exc_value, traceback):
    KillProcess(self._proc)


def GetLoadedModules(self, connection):
    modules = {}
    reply = connection.RspRequest('qXfer:libraries:read')
    self.assertEquals(reply[0], 'l')
    library_list = xml.etree.ElementTree.fromstring(reply[1:])
    self.assertEquals(library_list.tag, 'library-list')
    for library in library_list:
      self.assertEquals(library.tag, 'library')
      section = library.find('section')
      address = section.get('address')
      assert long(address) > 0
      modules[long(address)] = library.get('name')
    return modules


def ParseThreadStopReply(reply):
  match = re.match('T([0-9a-f]{2})thread-pcs:([0-9a-f]+);thread:([0-9a-f]+);$', reply)
  if match is None:
    raise AssertionError('Bad thread stop reply: %r' % reply)
  return {'signal': int(match.group(1), 16),
          'thread_pc': int(match.group(2), 16),
          'thread_id': int(match.group(3), 16)}


def AssertReplySignal(reply, signal):
  AssertEquals(ParseThreadStopReply(reply)['signal'], signal)


def ReadMemory(connection, address, size):
  reply = connection.RspRequest('m%x,%x' % (address, size))
  assert not reply.startswith('E'), reply
  return DecodeHex(reply)


def ReadUint32(connection, address):
  return struct.unpack('I', ReadMemory(connection, address, 4))[0]


class DebugStubTest(unittest.TestCase):

  def test_initial_breakpoint(self):
    # Any arguments to the nexe would work here because we are only
    # testing that we get a breakpoint at the _start entry point.
    # print('* test_initial_breakpoint')
    with LaunchDebugStub() as connection:
      reply = connection.RspRequest('?')
      AssertReplySignal(reply, NACL_SIGTRAP)

  def CheckModulesXml(self, connection):
    modules = GetLoadedModules(self, connection)
    assert modules.__len__ > 0

  # Test that we can fetch register values.
  # This check corresponds to the last instruction of debugger_test.c
  def CheckReadRegisters(self, connection, expectedPC):
    registers = DecodeRegs(connection.RspRequest('g'))
    self.assertEquals(registers['pc'], expectedPC)
    pc = struct.unpack('Q', DecodeHex(connection.RspRequest('p0')))[0]
    self.assertEquals(int(pc), expectedPC)

  # Test that reading from an unreadable address gives a sensible error.
  def CheckReadMemoryAtInvalidAddr(self, connection):
    mem_addr = 0xffffffff
    result = connection.RspRequest('m%x,%x' % (mem_addr, 1))
    self.assertEquals(result, 'E02')

  # Run tests on debugger_test.c binary.
  def test_debugger_test(self):
    with LaunchDebugStub() as connection:
      # Tell the process to continue, because it starts at the
      # breakpoint set at its start address.

      reply = connection.RspRequest('Z0,%x,1' % BREAK_ADDRESS_0)
      self.assertEquals("OK", reply)
      reply = connection.RspRequest('c')
      AssertReplySignal(reply, NACL_SIGTRAP)

      self.CheckModulesXml(connection)
      self.CheckReadRegisters(connection, BREAK_ADDRESS_0)

  def test_checking_thread_state(self):
    with LaunchDebugStub() as connection:
      # Query wasm thread id
      reply = connection.RspRequest('qfThreadInfo')
      match = re.match('m([0-9])$', reply)
      if match is None:
        raise AssertionError('Bad active thread list reply: %r' % reply)
      thread_id = int(match.group(1), 10)
      # There should not be other threads.
      reply = connection.RspRequest('qsThreadInfo')
      self.assertEquals("l", reply)
      # Test that valid thread should be alive.
      reply = connection.RspRequest('T%d' % (thread_id))
      self.assertEquals("OK", reply)
      # Test invalid thread id.
      reply = connection.RspRequest('T42')
      self.assertEquals("E02", reply)

  def test_reading_and_writing_memory(self):
    # Any arguments to the nexe would work here because we do not run
    # the executable beyond the initial breakpoint.
    with LaunchDebugStub() as connection:

      reply = connection.RspRequest('Z0,%x,1' % BREAK_ADDRESS_0)
      self.assertEquals("OK", reply)
      reply = connection.RspRequest('c')
      AssertReplySignal(reply, NACL_SIGTRAP)

      self.CheckReadMemoryAtInvalidAddr(connection)

      # Check reading code memory space.
      modules = GetLoadedModules(self, connection)
      expected_data = '\0asm'
      for module_address in modules.keys():
        result = ReadMemory(connection, module_address, len(expected_data))
        self.assertEquals(result, expected_data)

      # Check reading instance memory space.
      reply = connection.RspRequest('qWasmMem:%d;%x;%x' % (6, 0x12758, 4)) # 0x12758: width
      value = struct.unpack('I', DecodeHex(reply))[0]
      self.assertEquals(int(value), 12)

  def test_wasm_call_stack(self):
    with LaunchDebugStub() as connection:

      reply = connection.RspRequest('Z0,%x,1' % BREAK_ADDRESS_0)
      self.assertEquals("OK", reply)
      reply = connection.RspRequest('c')
      AssertReplySignal(reply, NACL_SIGTRAP)

      reply = connection.RspRequest('qWasmCallStack')
      stack = DecodeUInt64Array(reply)
      self.assertEquals(stack[0], BREAK_ADDRESS_0)
      self.assertEquals(stack[1], BREAK_ADDRESS_1)
      self.assertEquals(stack[2], BREAK_ADDRESS_2)

  def test_wasm_global(self):
    with LaunchDebugStub() as connection:

      reply = connection.RspRequest('Z0,%x,1' % BREAK_ADDRESS_0)
      self.assertEquals("OK", reply)
      reply = connection.RspRequest('c')
      AssertReplySignal(reply, NACL_SIGTRAP)

      reply = connection.RspRequest('qWasmGlobal:5;0')
      value = struct.unpack('Q', DecodeHex(reply))[0]
      self.assertTrue(value >= 0x10000 and value < 0x20000)

  def test_wasm_local(self):
    with LaunchDebugStub() as connection:

      reply = connection.RspRequest('Z0,%x,1' % BREAK_ADDRESS_0)
      self.assertEquals("OK", reply)
      reply = connection.RspRequest('c')
      AssertReplySignal(reply, NACL_SIGTRAP)

      reply = connection.RspRequest('qWasmLocal:5;0;0')
      value = struct.unpack('Q', DecodeHex(reply))[0]
      self.assertEquals(0x00012a08, value)
      reply = connection.RspRequest('qWasmLocal:5;1;0')
      value = struct.unpack('Q', DecodeHex(reply))[0]
      self.assertEquals(0x0000000c, value)

  def test_single_step(self):
    with LaunchDebugStub() as connection:
      reply = connection.RspRequest('Z0,%x,1' % BREAK_ADDRESS_0)
      self.assertEquals("OK", reply)
      reply = connection.RspRequest('c')
      AssertReplySignal(reply, NACL_SIGTRAP)

      # We expect test_single_step() to stop at the next instruction.
      reply = connection.RspRequest('s')
      AssertReplySignal(reply, NACL_SIGTRAP) # NACL_SIGSEGV?
      tid = ParseThreadStopReply(reply)['thread_id']
      self.assertEquals(tid, 1)
      # Skip past a 2-byte instruction.
      regs = DecodeRegs(connection.RspRequest('g'))
      self.assertEquals(regs['pc'], BREAK_ADDRESS_0 + 2)

      # Check that we can continue after single-stepping.
      reply = connection.RspRequest('Z0,%x,1' % BREAK_ADDRESS_0)
      self.assertEquals("OK", reply)

  def test_modifying_code_is_disallowed(self):
    with LaunchDebugStub() as connection:
      # Pick an arbitrary address in the code segment.
      func_addr = BREAK_ADDRESS_0
      # Writing to the code area should be disallowed.
      data = '\x00'
      write_command = 'M%x,%x:%s' % (func_addr, len(data), EncodeHex(data))
      reply = connection.RspRequest(write_command)
      self.assertEquals(reply, 'E03')

  def test_kill(self):
    sel_ldr = PopenDebugStub()
    try:
      connection = gdb_rsp.GdbRspConnection()
      # Request killing the target.
      reply = connection.RspRequest('k')
      self.assertEquals(reply, 'OK')
      self.assertEquals(sel_ldr.wait(), RETURNCODE_KILL)
    finally:
      KillProcess(sel_ldr)

  def test_detach(self):
    sel_ldr = PopenDebugStub()
    try:
      connection = gdb_rsp.GdbRspConnection()
      # Request detaching from the target.
      # This resumes execution, so we get the normal exit() status.
      reply = connection.RspRequest('D')
      self.assertEquals(reply, 'OK')
      self.assertEquals(sel_ldr.wait(), 1)
    finally:
      KillProcess(sel_ldr)

  def test_disconnect(self):
    sel_ldr = PopenDebugStub()
    try:
      # Connect and record the instruction pointer.
      connection = gdb_rsp.GdbRspConnection()
      # Store the instruction pointer.
      registers = DecodeRegs(connection.RspRequest('g'))
      initial_ip = registers['pc']
      connection.Close()
      # Reconnect 3 times.
      for _ in range(3):
        connection = gdb_rsp.GdbRspConnection()

        # Confirm the instruction pointer stays where it was, indicating that
        # the thread stayed suspended.
        registers = DecodeRegs(connection.RspRequest('g'))
        self.assertEquals(registers['pc'], initial_ip)
        connection.Close()
    finally:
      KillProcess(sel_ldr)


class DebugStubBreakpointTest(unittest.TestCase):

  def CheckInstructionPtr(self, connection, expected_ip):
    ip_value = DecodeRegs(connection.RspRequest('g'))['pc']
    self.assertEquals(ip_value, expected_ip)

  def test_setting_and_removing_breakpoint(self):
    func_addr = BREAK_ADDRESS_0
    with LaunchDebugStub() as connection:
      # Set a breakpoint.
      reply = connection.RspRequest('Z0,%x,1' % func_addr)
      self.assertEquals(reply, 'OK')
      # Requesting a breakpoint on an address that already has a
      # breakpoint should return an error.
      # reply = connection.RspRequest('Z0,%x,0' % func_addr)
      # self.assertEquals(reply, 'E03')

      # When we run the program, we should hit the breakpoint.  When
      # we continue, we should hit the breakpoint again because it has
      # not been removed: the debug stub does not step through
      # breakpoints automatically.
      for i in xrange(2):
        reply = connection.RspRequest('c')
        AssertReplySignal(reply, NACL_SIGTRAP)
        self.CheckInstructionPtr(connection, func_addr)

      # Check that we can remove the breakpoint.
      reply = connection.RspRequest('z0,%x,0' % func_addr)
      self.assertEquals(reply, 'OK')
      # Requesting removing a breakpoint on an address that does not
      # have one should return an error.
      reply = connection.RspRequest('z0,%x,0' % func_addr)
      self.assertEquals(reply, 'E03')
      # After continuing, we should not hit the breakpoint again, and
      # the program should run to completion.
      #reply = connection.RspRequest('c')
      #self.assertEquals(reply, 'W00')

  def test_setting_breakpoint_on_invalid_address(self):
    with LaunchDebugStub() as connection:
      # Requesting a breakpoint on an invalid address should give an error.
      reply = connection.RspRequest('Z0,%x,1' % (1 << 32))
      self.assertEquals(reply, 'E03')

  def test_setting_breakpoint_on_memory_address(self):
    with LaunchDebugStub() as connection:
      # Pick an arbitrary address in the data segment.
      data_addr = 0x500000000 # GetSymbols()['g_main_thread_var']
      # Requesting a breakpoint on a non-code address should give an error.
      reply = connection.RspRequest('Z0,%x,1' % data_addr)
      self.assertEquals(reply, 'E03')


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
