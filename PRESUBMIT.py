# Copyright 2018 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Presubmit script for changes in the infrastructure configs.

See http://dev.chromium.org/developers/how-tos/depottools/presubmit-scripts
for more details about the presubmit API built into gcl.
"""



def CheckLucicfgSemanticDiff(input_api, output_api):
  return [
      input_api.Command(
        'lucicfg semantic-diff',
        [
            'lucicfg' if not input_api.is_windows else 'lucicfg.bat',
            'semantic-diff',
            'main.star',
            'cr-buildbucket.cfg',
            'commit-queue.cfg',
            'luci-logdog.cfg',
            'luci-milo.cfg',
            'luci-scheduler.cfg',
            '-log-level', 'debug' if input_api.verbose else 'warning',
        ],
        {
          'stderr': input_api.subprocess.STDOUT,
          'shell': input_api.is_windows,  # to resolve *.bat
          'cwd': input_api.PresubmitLocalPath(),
        },
        output_api.PresubmitError)
  ]

def _CommonChecks(input_api, output_api):
  """Checks common to both upload and commit."""
  results = []
  results.extend(
      input_api.canned_checks.CheckChangedLUCIConfigs(input_api, output_api))
  results.extend(
      input_api.RunTests(CheckLucicfgSemanticDiff(input_api, output_api)))
  return results


def CheckChangeOnUpload(input_api, output_api):
  results = []
  results.extend(_CommonChecks(input_api, output_api))
  return results


def CheckChangeOnCommit(input_api, output_api):
  results = []
  results.extend(_CommonChecks(input_api, output_api))
  return results
