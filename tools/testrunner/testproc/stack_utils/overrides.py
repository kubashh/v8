#!/usr/bin/env python3
# Copyright 2023 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import re


def filter_stack_frame_override(stack_frame):
  """Filter stack frame."""
  # Filter out anonymous namespaces.
  anonymous_namespaces = [
      'non-virtual thunk to ',
      '(anonymous namespace)::',
      '`anonymous namespace\'::',
  ]
  for ns in anonymous_namespaces:
    stack_frame = stack_frame.replace(ns, '')

  # Rsplit around '!'.
  stack_frame = stack_frame.split('!')[-1]

  # Lsplit around '(', '['.
  m = re.match(r'(.*?)[\[].*', stack_frame)
  if m and m.group(1):
    return m.group(1).strip()

  # Lsplit around ' '.
  stack_frame = stack_frame.strip().split(' ')[0]

  return stack_frame


constants_override = {
    'SAN_STACK_FRAME_REGEX':
        re.compile(
            # frame id (1)
            r'\s*#(?P<frame_id>\d+)\s+'
            # addr (2)
            r'([xX0-9a-fA-F]*)\s*'
            # Format is [in {fun}[+{off}]] [{file}[:{line}[:{char}]]] [({mod}[+{off}])]
            # If there is fun and mod/file info, extract
            # fun+off, where fun (7, 5, 23), off (8)
            r'(([in\s*]*(((.*)\+([xX0-9a-fA-F]+))|(.*)) '
            r'('
            # file:line:char, where file (12, 16), line (13, 17), char (14)
            r'(([^ ]+):(\d+):(\d+))|(([^ ]+):(\d+))'
            # or mod+off, where mod (19, 31), off (21, 32)
            r'|'
            r'(\(([^+]+)(\+([xX0-9a-fA-F]+))?\)))'
            r')'
            # If there is only fun info, extract
            r'|'
            r'([in\s*]*(((.*)\+([xX0-9a-fA-F]+))|(.*)))'
            # If there is only mod info, extract
            r'|'
            r'(\((((.*)\+([xX0-9a-fA-F]+))|(.*))\))'
            r')')
}
