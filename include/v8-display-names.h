// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INCLUDE_V8_DISPLAY_NAMES_H_
#define INCLUDE_V8_DISPLAY_NAMES_H_

#include "v8-local-handle.h"  // NOLINT(build/include_directory)

namespace v8 {

class Isolate;

/**
 * An instance of display names from the embedder.
 */
class DisplayNames {
 public:
  virtual ~DisplayNames() {}
  virtual V8_WARN_UNUSED_RESULT MaybeLocal<String> Of(Isolate* isolate,
                                                      const char* code) = 0;
};

}  // namespace v8

#endif  // INCLUDE_V8_DISPLAY_NAMES_H_
