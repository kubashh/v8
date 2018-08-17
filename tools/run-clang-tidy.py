#!/usr/bin/env python
#
# Copyright 2012 the V8 project authors. All rights reserved.
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
#       copyright notice, this list of conditions and the following
#       disclaimer in the documentation and/or other materials provided
#       with the distribution.
#     * Neither the name of Google Inc. nor the names of its
#       contributors may be used to endorse or promote products derived
#       from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

import optparse
import re
import subprocess
import os
import sys

CLANG_TIDY_WARNING = re.compile(r"(\/.*?)\ .*\[(.*)\]$")

class CTWarning(object):

    def __init__(self, warning_type):
        self.warning_type = warning_type
        self.occurrences = set()

    def AddOccurrence(self, file_path):
        self.occurrences.add(file_path)

    def __hash__(self):
        return hash(self.warning_type)

    def ToString(self, file_loc):
        s = ""
        s +=  "[" + self.warning_type + "] #" + str(len(self.occurrences)) + "\n"
        if file_loc:
          s +=  "  " + '\n  '.join(self.occurrences)
        return s

    def __str__(self):
        return self.ToString(False)

    def __lt__(self, other):
        return len(self.occurrences) < len(other.occurrences)


def GenerateCompileCommands(build_folder):
    print build_folder
    ninja_ps = subprocess.Popen(["ninja", "-t", "compdb", "cxx", "cc"], stdout=subprocess.PIPE, cwd=build_folder)
    with open("compile_commands.json", "w") as cc_file:
        sed_expr = 's/\(\-fcomplete-member-pointers\|-Wno-enum-compare-switch\|-Wno-ignored-pragma-optimize\|-Wno-null-pointer-arithmetic\|-Wno-unused-lambda-capture\)//g'
        print sed_expr
        sed_ps = subprocess.Popen(["sed", "-e", sed_expr],\
                                  stdin=ninja_ps.stdout, stdout=cc_file)
        ninja_ps.wait()
        sed_ps.wait()


def ClangTidyRunFull(build_folder):
    subprocess.call(["run-clang-tidy", "-j72", "-p", "."], cwd=build_folder)


def ClangTidyRunAggregate(build_folder, print_files):
    with open(os.devnull, "w") as DEVNULL:
      ct_process = subprocess.Popen(["run-clang-tidy", "-j72", "-p", "."],
                                    cwd=build_folder,
                                    stdout=subprocess.PIPE,
                                    stderr=DEVNULL)
    warnings = dict()
    while True:
        line = ct_process.stdout.readline()
        if line != '':
            res = CLANG_TIDY_WARNING.search(line)
            if res is not None:
                if res.group(2) in warnings:
                    current_warning = warnings[res.group(2)]
                    current_warning.AddOccurrence(res.group(1))
                else:
                    new_warning =  CTWarning(res.group(2))
                    new_warning.AddOccurrence(res.group(1))
                    warnings[res.group(2)] = new_warning
        else:
            break
    for warning in sorted(warnings.values(), reverse=True):
        sys.stdout.write(warning.ToString(print_files))


def ClangTidyRunDiff(diff_branch):
    git_ps = subprocess.Popen(["git", "diff", "-U0", diff_branch], stdout=subprocess.PIPE)
    ct_ps = subprocess.Popen(["clang-tidy-diff.py", "-p", ".", "-p1"], stdin=git_ps.stdout)
    git_ps.wait()
    ct_ps.wait()
    return True


def CheckClangTidy():
    with open(os.devnull, "w") as DEVNULL:
        return subprocess.call(["which", "clang-tidy"], stdout=DEVNULL) == 0
    return False


def GetOptions():
    result = optparse.OptionParser()
    result.add_option('-b', '--build-folder', help='Set V8 build folder', dest='build_folder',
                      default='out.gn/x64.release/')
    result.add_option('--ct-full', help='Run clang-tidy on the whole codebase',
                      default=False, action='store_true')
    result.add_option('--ct-aggregate', help='Run clang-tidy on the whole \
                      codebase and aggregate the warnings',
                      default=False, action='store_true')
    result.add_option('--ct-show-loc', help='Show file locations when running \
                      in aggregate mode', default=False, action='store_true')
    result.add_option('--branch', help='Run clang-tidy on the diff between HEAD and DIFF_BRANCH',
                      default='master', dest='diff_branch')
    result.add_option('--gen-comdb', help='Generate a compilation database for clang-tidy',
                      default=False, action='store_true')
    return result


def main():
  parser = GetOptions()
  (options, _) = parser.parse_args()

  if not CheckClangTidy():
    print "Could not find clang-tidy"
  elif options.gen_comdb:
    GenerateCompileCommands(options.build_folder)
  else:
    print options.build_folder
    if options.ct_full:
        print "Running clang-tidy - full"
        ClangTidyRunFull(options.build_folder)
    elif options.ct_aggregate:
        print "Running clang-tidy - aggregating warnings"
        ClangTidyRunAggregate(options.build_folder, options.ct_show_loc)
    else:
        print "Running clang-tidy"
        ClangTidyRunDiff(options.diff_branch)

if __name__ == "__main__":
    main()
