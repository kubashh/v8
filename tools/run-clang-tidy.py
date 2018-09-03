#!/usr/bin/env python
#
# Copyright 2018 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import json
import optparse
import os
import re
import subprocess
import sys

CLANG_TIDY_WARNING = re.compile(r"(\/.*?)\ .*\[(.*)\]$")
CLANG_TIDY_CMDLINE_OUT = re.compile(r"^clang-tidy.*\ .*|^\./\.\*")

THREADS = 1

class CTWarning(object):
    """
    Wraps up a clang-tidy warning to present aggregated information.
    """

    def __init__(self, warning_type):
        self.warning_type = warning_type
        self.occurrences = set()

    def AddOccurrence(self, file_path):
        self.occurrences.add(file_path)

    def __hash__(self):
        return hash(self.warning_type)

    def ToString(self, file_loc):
        s = ""
        s += "[" + self.warning_type + "] #" + str(len(self.occurrences)) + "\n"
        if file_loc:
          s +=  "  " + '\n  '.join(self.occurrences)
          s += '\n'
        return s

    def __str__(self):
        return self.ToString(False)

    def __lt__(self, other):
        return len(self.occurrences) < len(other.occurrences)


def GenerateCompileCommands(build_folder):
    """
    Generate a compilation database.

    Currently clang-tidy-4 does not understand all flags that are passed
    by the build system, therefore, we them out of the generated file.
    """
    ninja_ps = subprocess.Popen(["ninja", "-t", "compdb", "cxx", "cc"],
                                stdout=subprocess.PIPE, cwd=build_folder)
    with open(os.path.join(build_folder, "compile_commands.json"), "w") \
            as cc_file:
        sed_expr = 's/\(\-fcomplete-member-pointers\|-Wno-enum-compare-switch\|'\
                   '-Wno-ignored-pragma-optimize\|-Wno-null-pointer-arithmetic\|'\
                   '-Wno-unused-lambda-capture\)//g'
        sed_ps = subprocess.Popen(["sed", "-e", sed_expr],\
                                  stdin=ninja_ps.stdout, stdout=cc_file)
        ninja_ps.wait()
        sed_ps.wait()


def skip_line(line):
    """
    Check if a clang-tidy output line should be skipped.
    """
    res = CLANG_TIDY_CMDLINE_OUT.search(line)
    if res is not None:
        return True


def ClangTidyRunFull(build_folder, skip_output_filter):
    """
    Run clang-tidy on the full codebase and print warnings.
    """
    with open(os.devnull, "w") as DEVNULL:
        ct_process = subprocess.Popen(["run-clang-tidy",
                                       "-j" + str(THREADS), "-p", "."],
                                       cwd=build_folder,
                                       stdout=subprocess.PIPE,
                                       stderr=DEVNULL)
    removing_check_header = False
    empty_lines = 0

    while True:
        line = ct_process.stdout.readline()
        if line != '':
            # Skip all lines after Enbale checks and before two newlines,
            # i.e., skip clang-tidy check list.
            if line.startswith('Enabled checks'):
                removing_check_header = True
            if removing_check_header and not skip_output_filter:
                if line == '\n':
                    empty_lines += 1
                if empty_lines == 2:
                    removing_check_header = False
                continue

            # different lines get removed to ease output reading
            if not skip_output_filter and skip_line(line):
                continue

            # print line, because no filter was matched
            sys.stdout.write(line)
        else:
            break



def ClangTidyRunAggregate(build_folder, print_files):
    """
    Run clang-tidy on the full codebase and aggregate warnings into categories.
    """
    with open(os.devnull, "w") as DEVNULL:
      ct_process = subprocess.Popen(["run-clang-tidy",
                                     "-j" + str(THREADS), "-p", "."],
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
    """
    Run clang-tidy on the diff between current and the diff_branch.
    """
    if diff_branch is None:
        git_mb = subprocess.Popen(["git", "merge-base", "HEAD",
                                   "origin/master"], stdout=subprocess.PIPE)
        git_mb.wait()
        diff_branch = git_mb.stdout.readline().strip()
    git_ps = subprocess.Popen(["git", "diff", "-U0", diff_branch],
                              stdout=subprocess.PIPE)
    with open(os.devnull, "w") as DEVNULL:
        ct_ps = subprocess.Popen(["clang-tidy-diff.py", "-p", ".", "-p1"],
                                 stdin=git_ps.stdout,
                                 stdout=subprocess.PIPE, stderr=DEVNULL)
    git_ps.wait()
    while True:
        line = ct_ps.stdout.readline()
        if line != '':
            if skip_line(line):
                continue

            sys.stdout.write(line)
        else:
            break

def rm_prefix(string, prefix):
    """
    Removes prefix from a string until the new string
    nolonger starts with the prefix.
    """
    while string.startswith(prefix):
        string = string[len(prefix):]
    return string


def ClangTidyRunSingleFile(build_folder, filename_to_check, line_ranges=[]):
    """
    Run clang-tidy on a single file.
    """
    files_with_relative_path = []

    compdb = json.load(open(os.path.join(build_folder,
                                         "compile_commands.json")))
    for db_entry in compdb:
        if db_entry['file'].endswith(filename_to_check):
            files_with_relative_path.append(db_entry['file'])

    with open(os.devnull, "w") as DEVNULL:
        for file_with_relative_path in files_with_relative_path:
            line_filter = None
            if len(line_ranges) != 0:
                line_filter = "["
                line_filter += "{ \"lines\":[" + ", ".join(line_ranges)
                line_filter += "], \"name\":\""
                line_filter += rm_prefix(file_with_relative_path, "../") + "\"}"
                line_filter += "]"

            if line_filter:
                ct_ps = subprocess.call(["clang-tidy", "-p", ".",
                                         "-line-filter=" + line_filter,
                                         file_with_relative_path],
                                        cwd=build_folder, stderr=DEVNULL)
            else:
                ct_ps = subprocess.call(["clang-tidy", "-p", ".",
                                         file_with_relative_path],
                                        cwd=build_folder, stderr=DEVNULL)


def CheckClangTidy():
    """
    Checks if a clang-tidy binary exists.
    """
    with open(os.devnull, "w") as DEVNULL:
        return subprocess.call(["which", "clang-tidy"], stdout=DEVNULL) == 0
    return False


def CheckCompDB(build_folder):
    """
    Checks if a compilation database exists in the build_folder.
    """
    return os.path.isfile(os.path.join(build_folder, "compile_commands.json"))


def GetOptions():
    """
    Generate the option parser for this script.
    """
    result = optparse.OptionParser()
    result.add_option('-b', '--build-folder', help='Set V8 build folder',
                      dest='build_folder', default='out.gn/x64.release/')
    result.add_option('-j', help='Set V8 build folder', dest='threads',
                      default=4)
    result.add_option('--gen-compdb',
                      help='Generate a compilation database for clang-tidy',
                      default=False, action='store_true')
    result.add_option('--no-output-filter',
                      help='Done use any output filterning',
                      default=False, action='store_true')

    # Full clang-tidy
    full_run_g = optparse.OptionGroup(result, "Clang-tidy full", "")
    full_run_g.add_option('--full', help='Run clang-tidy on the whole codebase',
                          default=False, action='store_true')
    result.add_option_group(full_run_g)

    # Aggregate clang-tidy
    agg_run_g = optparse.OptionGroup(result, "Clang-tidy aggregate", "")
    agg_run_g.add_option('--aggregate', help='Run clang-tidy on the whole '\
                         'codebase and aggregate the warnings',
                         default=False, action='store_true')
    agg_run_g.add_option('--show-loc', help='Show file locations when running '\
                         'in aggregate mode', default=False,
                         action='store_true')
    result.add_option_group(agg_run_g)

    # Diff clang-tidy
    diff_run_g = optparse.OptionGroup(result, "Clang-tidy diff", "")
    diff_run_g.add_option('--branch', help='Run clang-tidy on the diff '\
                          'between HEAD and the merge-base between HEAD '\
                          'and DIFF_BRANCH (origin/master by default).',
                          default=None, dest='diff_branch')
    result.add_option_group(diff_run_g)

    # Single clang-tidy
    single_run_g = optparse.OptionGroup(result, "Clang-tidy single", "")
    single_run_g.add_option('--single', help="", default=False,
                            action='store_true')
    single_run_g.add_option('--file', help="File name to check", default=None,
                            dest='file_name')
    single_run_g.add_option('--lines', help="Limit checks to a line range. "\
                            "For example: --lines='[2,4], [5,6]'",
                            default=[], dest='line_ranges')

    result.add_option_group(single_run_g)
    return result


def main():
  parser = GetOptions()
  (options, _) = parser.parse_args()

  global THREADS
  THREADS = options.threads

  if not CheckClangTidy():
    print "Could not find clang-tidy"
  elif options.gen_compdb:
    GenerateCompileCommands(options.build_folder)
  elif not CheckCompDB(options.build_folder):
    print "Could not find compilatin database, " \
        "please generate it with --gen-compdb"
  else:
    print options.build_folder
    if options.full:
        print "Running clang-tidy - full"
        ClangTidyRunFull(options.build_folder, options.no_output_filter)
    elif options.aggregate:
        print "Running clang-tidy - aggregating warnings"
        ClangTidyRunAggregate(options.build_folder, options.show_loc)
    elif options.single:
        print "Running clang-tidy - single on " + options.file_name
        if options.file_name is not None:
            line_ranges = []
            for match in re.findall(r"(\[.*?\])", options.line_ranges):
                if match is not []:
                    line_ranges.append(match)
            ClangTidyRunSingleFile(options.build_folder, options.file_name,
                                   line_ranges)
    else:
        print "Running clang-tidy"
        ClangTidyRunDiff(options.diff_branch)

if __name__ == "__main__":
    main()
