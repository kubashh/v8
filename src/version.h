// Copyright 2009 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_VERSION_H_
#define V8_VERSION_H_

#include "src/base/functional.h"
#include "src/vector.h"

namespace v8 {
namespace internal {

class Version {
 public:
  // Return the various version components.
  static int GetMajor() { return major_; }
  static int GetMinor() { return minor_; }
  static int GetBuild() { return build_; }
  static int GetPatch() { return patch_; }
  static bool IsCandidate() { return candidate_; }
  static uint32_t Hash() {
    return static_cast<uint32_t>(
        base::hash_combine(major_, minor_, build_, patch_));
  }

  static const char* GetVersion() { return version_string_; }
 private:
  static const int major_;
  static const int minor_;
  static const int build_;
  static const int patch_;
  static const bool candidate_;
  static const char* version_string_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_VERSION_H_
