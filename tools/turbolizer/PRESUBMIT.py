# Copyright 2018 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import subprocess


def CheckChangeOnCommit(input_api, output_api):
    results = []

    p = subprocess.Popen('npm run-script --silent presubmit',
                         stdout=subprocess.PIPE, stderr=subprocess.PIPE, shell=True)
    output, err = p.communicate()
    if p.returncode != 0:
        results.append(output_api.PresubmitError(
            '`npm run presubmit` found errors in Turbolizer.',
            [err]))

    return results
