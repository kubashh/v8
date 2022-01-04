# Copyright 2021 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

workspace(name = "v8")

load("@bazel_tools//tools/build_defs/repo:git.bzl", "new_git_repository")
load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

http_archive(
    name = "bazel_skylib",
    urls = [
        "https://github.com/bazelbuild/bazel-skylib/releases/download/1.0.3/bazel-skylib-1.0.3.tar.gz",
        "https://mirror.bazel.build/github.com/bazelbuild/bazel-skylib/releases/download/1.0.3/bazel-skylib-1.0.3.tar.gz",
    ],
    sha256 = "1c531376ac7e5a180e0237938a2536de0c54d93f5c278634818e0efc952dd56c",
)
load("@bazel_skylib//:workspace.bzl", "bazel_skylib_workspace")
bazel_skylib_workspace()

new_git_repository(
    name = "com_googlesource_chromium_icu",
    build_file = "//:bazel/BUILD.icu",
    commit = "fbc6faf1c2c429cd27fabe615a89f0b217aa4213",
    remote = "https://chromium.googlesource.com/chromium/deps/icu.git",
)

new_git_repository(
    name = "com_googlesource_chromium_trace_event_common",
    build_file = "//:bazel/BUILD.trace_event_common",
    commit = "7f36dbc19d31e2aad895c60261ca8f726442bfbb",
    remote = "https://chromium.googlesource.com/chromium/src/base/trace_event/common.git",
)

new_git_repository(
    name = "com_googlesource_chromium_zlib",
    build_file = "//:bazel/BUILD.zlib",
    commit = "efd9399ae01364926be2a38946127fdf463480db",
    remote = "https://chromium.googlesource.com/chromium/src/third_party/zlib.git",
)
