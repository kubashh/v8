# Copyright 2020 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

load("//lib/lib.star", "branch_descriptors")

luci.gitiles_poller(
    name = "chromium-trigger",
    bucket = "ci",
    repo = "https://chromium.googlesource.com/chromium/src",
    refs = ["refs/heads/master"],
)

def branch_poolers():
    for bucket_name, branch in branch_descriptors.items():
        luci.gitiles_poller(
            name = branch.pooler_name,
            bucket = bucket_name,
            repo = "https://chromium.googlesource.com/v8/v8",
            refs = branch.refs,
        )

branch_poolers()

luci.gitiles_poller(
    name = "v8-trigger-official",
    bucket = "ci",
    repo = "https://chromium.googlesource.com/v8/v8",
    refs = [
        "refs/branch-heads/\\d+\\.\\d+",
        "refs/heads/\\d+\\.\\d+\\.\\d+",
    ],
)

luci.gitiles_poller(
    name = "v8-trigger-branches-auto-tag",
    bucket = "ci",
    repo = "https://chromium.googlesource.com/v8/v8",
    refs = ["refs/branch-heads/\\d+\\.\\d+"],
)
