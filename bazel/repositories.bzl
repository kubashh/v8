# Copyright 2021 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

load("@bazel_tools//tools/build_defs/repo:git.bzl", "new_git_repository")
load("@bazel_tools//tools/build_defs/repo:utils.bzl", "maybe")

def v8_repositories():
    maybe(
        new_git_repository,
        name = "com_googlesource_chromium_base_trace_event_common",
        build_file = "@v8//:bazel/BUILD.trace_event_common",
        commit = "7f36dbc19d31e2aad895c60261ca8f726442bfbb",
        remote = "https://chromium.googlesource.com/chromium/src/base/trace_event/common.git",
    )

    maybe(
        new_git_repository,
        name = "com_googlesource_chromium_icu",
        build_file = "@v8//:bazel/BUILD.icu",
        commit = "fbc6faf1c2c429cd27fabe615a89f0b217aa4213",
        remote = "https://chromium.googlesource.com/chromium/deps/icu.git",
    )

    maybe(
        new_git_repository,
        name = "com_googlesource_chromium_zlib",
        build_file = "@v8//:bazel/BUILD.zlib",
        commit = "efd9399ae01364926be2a38946127fdf463480db",
        remote = "https://chromium.googlesource.com/chromium/src/third_party/zlib.git",
    )
