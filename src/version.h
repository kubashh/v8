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

class Version {
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
 private:
  static constexpr int major_ = V8_MAJOR_VERSION;
  static constexpr int minor_ = V8_MINOR_VERSION;
  static constexpr int build_ = V8_BUILD_NUMBER;
  static constexpr int patch_ = V8_PATCH_LEVEL;
  static constexpr bool candidate_ = (V8_IS_CANDIDATE_VERSION != 0);
  static const char* version_string_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_VERSION_H_
