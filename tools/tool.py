#!/usr/bin/env python
# Copyright 2016 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Script to generate V8's gn arguments based on common developer defaults
or builder configurations.

Examples:

# Generate the x64.release config in out.gn/x64.release.
tools/tool.py x64.release

# Generate into out.gn/foo.
tools/tool.py -b x64.release foo

# Pass additional gn arguments after -- (don't use spaces within gn args).
tools/tool.py x64.optdebug -- use_goma=true

# Print wrapped commands.
tools/tool.py x64.optdebug -v

# Print output of wrapped commands.
tools/tool.py x64.optdebug -vv

# Generate gn arguments of builder 'V8 Linux64 - builder' from master
# 'client.v8' into out.gn/V8_Linux64___builder.
tools/tool.py -m client.v8 -b 'V8 Linux64 - builder'
"""

import argparse
import os
import subprocess
import sys

BASE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_DIR = 'out.gn'


def _sanitize_nonalpha(text):
  return ''.join(c if (c.isalnum() or c == '.') else '_' for c in text)


class GenerateGnArgs(object):
  def __init__(self, args):
    index = args.index('--') if '--' in args else len(args)
    script_args = args[:index]
    self.gn_args = args[index + 1:]

    parser = argparse.ArgumentParser(
      description='Generator for V8\'s gn arguments. '
                  'Additional gn args can be separated with -- '
                  'from the arguments to this tool.')
    parser.add_argument(
        'outdir', nargs='?',
        help='optional gn output directory')
    parser.add_argument(
        '-b', '--builder',
        help='build configuration or builder name from mb_config.pyl')
    parser.add_argument(
        '-m', '--master', default='developer_default',
        help='config group or master from mb_config.pyl')
    parser.add_argument(
        '-v', '--verbosity', action='count',
        help='increase output verbosity (can be specified multiple times)')
    self.options = parser.parse_args(script_args)

    assert (self.options.outdir or self.options.builder), (
        'please specify either an output directory or a builder/config name, '
        'e.g. x64.release')

    if not self.options.outdir:
      # Derive output directory from builder name.
      self.options.outdir = _sanitize_nonalpha(self.options.builder)
    else:
      # Also, if this should work on windows, we might need to use \ where
      # outdir is used as path, while using / if it's used in a gn context.
      assert not self.options.outdir.startswith('/'), (
        'only output directories relative to %s are supported' % OUT_DIR)

    if not self.options.builder:
      # Derive builder from output directory.
      self.options.builder = self.options.outdir

  def verbose_print_1(self, text):
    if self.options.verbosity >= 1:
      print '#################################################################'
      print text

  def verbose_print_2(self, text):
    if self.options.verbosity >= 2:
      print text

  def call_cmd(self, args):
    self.verbose_print_1(' '.join(args))
    try:
      output = subprocess.check_output(
        args=args,
        stderr=subprocess.STDOUT,
      )
      self.verbose_print_2(output)
    except subprocess.CalledProcessError as e:
      self.verbose_print_2(e.output)
      raise

  def main(self):
    # Always operate relative to the base directory for better relative-path
    # handling.
    self.verbose_print_1('cd ' + BASE_DIR)
    os.chdir(BASE_DIR)

    # The directories are separated with slashes in a gn context (platform
    # independent).
    gn_outdir = '/'.join([OUT_DIR, self.options.outdir])

    # Call MB to generate the basic configuration.
    self.call_cmd([
      sys.executable,
      '-u', os.path.join('tools', 'mb', 'mb.py'),
      'gen',
      '-f', os.path.join('infra', 'mb', 'mb_config.pyl'),
      '-m', self.options.master,
      '-b', self.options.builder,
      gn_outdir,
    ])

    # Handle extra gn arguments.
    if self.gn_args:
      gn_args_path = os.path.join(OUT_DIR, self.options.outdir, 'args.gn')
      more_gn_args = '\n'.join(self.gn_args)

      # Append extra gn arguments to the generated args.gn file.
      self.verbose_print_1("Appending \"\"\"\n%s\n\"\"\" to %s." % (
          more_gn_args, os.path.abspath(gn_args_path)))
      with open(gn_args_path, 'a') as f:
        f.write('\n# Additional command-line args:\n')
        f.write(more_gn_args)
        f.write('\n')

      # Regenerate ninja files to check for errors in the additional gn args.
      self.call_cmd(['gn', 'gen', gn_outdir])
    return 0

if __name__ == "__main__":
  gen = GenerateGnArgs(sys.argv[1:])
  try:
    sys.exit(gen.main())
  except Exception:
    if gen.options.verbosity < 2:
      print ('\nHint: You can raise verbosity (-vv) to see the output of '
             'failed commands.\n')
    raise
