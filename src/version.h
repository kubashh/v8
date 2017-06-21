// Copyright 2009 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_VERSION_H_
#define V8_VERSION_H_

#include "include/v8-version.h"
#include "src/base/functional.h"
#include "src/vector.h"

namespace v8 {
namespace internal {

template <int Major, int Minor, int Build, int Patch, bool Candidate>
class VersionImpl {
 public:
  // Return the various version components.
  static constexpr int GetMajor() { return major_; }
  static constexpr int GetMinor() { return minor_; }
  static constexpr int GetBuild() { return build_; }
  static constexpr int GetPatch() { return patch_; }
  static constexpr bool IsCandidate() { return candidate_; }
  static uint32_t Hash() {
    return static_cast<uint32_t>(
        base::hash_combine(major_, minor_, build_, patch_));
  }

  static const char* GetVersion() { return version_string_; }

  // Methods for test-version.cc.
  static void CheckVersion(const char* expected_version_string,
                           const char* expected_generic_soname);

  // Calculate the V8 version string.
  static void GetString(Vector<char> str);

  // Calculate the SONAME for the V8 shared library.
  static void GetSONAME(Vector<char> str);

 private:
  static constexpr int major_ = Major;
  static constexpr int minor_ = Minor;
  static constexpr int build_ = Build;
  static constexpr int patch_ = Patch;
  static constexpr bool candidate_ = Candidate;
  static const char* soname_;
  static const char* version_string_;
};

using Version = VersionImpl<V8_MAJOR_VERSION, V8_MINOR_VERSION, V8_BUILD_NUMBER,
                            V8_PATCH_LEVEL, (V8_IS_CANDIDATE_VERSION != 0)>;

}  // namespace internal
}  // namespace v8

#endif  // V8_VERSION_H_
