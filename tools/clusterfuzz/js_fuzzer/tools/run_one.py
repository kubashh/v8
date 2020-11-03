#!/usr/bin/env python
# Copyright 2020 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.


"""
Helper script to execute fuzz tests in a single process.

Expects fuzz tests in workdir/output/dir-<dir number>/fuzz-XXX.js.
Expects the <dir number> as single parameter.
"""

import json
import os
import random
import re
import subprocess
import sys

from v8_commands import Execute

STACKTRACE_TOOL_MARKERS = [
    ' runtime error: ',
    'AddressSanitizer',
    'ASAN:',
    'CFI: Most likely a control flow integrity violation;',
    'ERROR: libFuzzer',
    'KASAN:',
    'LeakSanitizer',
    'MemorySanitizer',
    'ThreadSanitizer',
    'UndefinedBehaviorSanitizer',
    'UndefinedSanitizer',
]
STACKTRACE_END_MARKERS = [
    'ABORTING',
    'END MEMORY TOOL REPORT',
    'End of process memory map.',
    'END_KASAN_OUTPUT',
    'SUMMARY:',
    'Shadow byte and word',
    '[end of stack trace]',
    '\nExiting',
    'minidump has been written',
]
CHECK_FAILURE_MARKERS = [
    'Check failed:',
    'Device rebooted',
    'Fatal error in',
    'FATAL EXCEPTION',
    'JNI DETECTED ERROR IN APPLICATION:',
    'Sanitizer CHECK failed:',
]

BASE_PATH = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FOOZZIE = os.path.join(BASE_PATH, 'workdir2', 'app_dir', 'd8')
TEST_CASES = os.path.join(BASE_PATH, 'workdir2', 'output')

# Output pattern from foozzie.py when it finds a failure.
FAILURE_RE = re.compile(
    r'# V8 correctness failure.'
    r'# V8 correctness configs: (?P<configs>.*).'
    r'# V8 correctness sources: (?P<source>.*).'
    r'# V8 correctness suppression:.*', re.S)

ARGS = '--fuzzing --expose-gc --allow-natives-syntax --debug-code --es-staging --wasm-staging --disable-abortjs --omit-quit --disable-in-process-stack-traces --invoke-weak-callbacks --enable-slow-asserts --verify-heap'.split()

assert(len(sys.argv) > 1)
dir_number = int(sys.argv[1])
assert(dir_number >= 0)

test_dir = os.path.join(TEST_CASES, 'dir-%d' % dir_number)
assert os.path.exists(test_dir)

def failure_state(command, marker):
  return dict(marker=marker, command=command, dupes=0)

def random_seed():
  """Returns random, non-zero seed."""
  seed = 0
  while not seed:
    seed = random.SystemRandom().randint(-2147483648, 2147483647)
  return seed

def run(fuzz_file, flag_file):
  """Executes the differential-fuzzing harness foozzie with one fuzz test."""
  try:
    with open(flag_file) as f:
      flags = f.read().split(' ')
  except:
    flags = []
  cmd = [FOOZZIE, '--random-seed=%d' % random_seed()] + ARGS + flags + [fuzz_file]
  # print(' '.join(cmd))
  output = Execute(
      cmd, cwd=os.path.dirname(os.path.abspath(fuzz_file)), timeout=10)

  return ' '.join(cmd), output

def list_tests():
  """Iterates all fuzz tests and corresponding flags in the given base dir."""
  for f in os.listdir(test_dir):
    if f.startswith('fuzz'):
      n = int(re.match(r'fuzz-(\d+)\.js', f).group(1))
      ff = 'flags-%d.js' % n
      yield (os.path.join(test_dir, f), os.path.join(test_dir, ff))

def has_marker(stacktrace, marker_list):
  """Return true if the stacktrace has atleast one marker
  in the marker list."""
  for marker in marker_list:
    if marker in stacktrace:
      return marker
  return None

# Some counters for the statistics.
count = 0
count_timeout = 0
count_failure = 0
failures = []

# Execute all tests in the given directory. Interpret foozzie's output and add
# it to the statistics.
for fuzz_file, flag_file in list_tests():
  cmd, output = run(fuzz_file, flag_file)
  count += 1
  if output.timeout:
    count_timeout += 1
    continue

  error = None
  tool = has_marker(output.stdout, STACKTRACE_TOOL_MARKERS)
  end = has_marker(output.stdout, STACKTRACE_END_MARKERS)
  check = has_marker(output.stdout, CHECK_FAILURE_MARKERS)
  if 'Fatal javascript OOM' in output.stdout:
    error = 'OOM'
  elif check:
    error = check
  elif tool and end:
    error = tool + '_' + end
  elif output.HasCrashed():
    error = 'crash'
  if error:
    count_failure += 1
    failures.append(failure_state(cmd, error))

with open(os.path.join(test_dir, 'failures.json'), 'w') as f:
  json.dump(failures, f)

stats = {
  'total': count,
  'timeout': count_timeout,
  'failure': count_failure,
}

with open(os.path.join(test_dir, 'stats.json'), 'w') as f:
  json.dump(stats, f)
