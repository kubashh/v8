#!/usr/bin/env python

# Copyright 2018 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""
Converts a given file in clang assembly syntax to a corresponding
representation in inline assembly. Specifically, this is used to convert
embedded.S to embedded.cc for Windows clang builds.
"""

import string
import sys

def usage():
  print "Usage: asm_to_inline_asm.py <input file> <output file>"
  sys.exit(1);

def asm_to_inl_asm(in_filename, out_filename):
  with open(in_filename, 'r') as infile:
    with open(out_filename, 'wb') as outfile:
      outfile.write("__asm__(\n")
      for line in infile:
        outfile.write("  \"{}\\n\"\n".format(string.rstrip(line)))
      outfile.write(");\n")

if __name__ == '__main__':
  if len(sys.argv) != 3: usage()
  in_filename = sys.argv[1];
  out_filename = sys.argv[2];
  sys.exit(asm_to_inl_asm(in_filename, out_filename))
