// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BASE_PLATFORM_WRAPPERS_H_
#define V8_BASE_PLATFORM_WRAPPERS_H_

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef V8_OS_STARBOARD
#include "starboard/string.h"
#endif  // V8_OS_STARBOARD

namespace v8::base {

inline char* Strdup(const char* source) {
#ifdef V8_OS_STARBOARD
  return SbStringDuplicate(source);
#else   // !V8_OS_STARBOARD
  return strdup(source);
#endif  // !V8_OS_STARBOARD
}

inline FILE* Fopen(const char* filename, const char* mode) {
#ifdef V8_OS_STARBOARD
  return NULL;
#else   // !V8_OS_STARBOARD
  return fopen(filename, mode);
#endif  // !V8_OS_STARBOARD
}

inline int Fclose(FILE* stream) {
#ifdef V8_OS_STARBOARD
  return -1;
#else   // !V8_OS_STARBOARD
  return fclose(stream);
#endif  // !V8_OS_STARBOARD
}

}  // namespace v8::base

#endif  // V8_BASE_PLATFORM_WRAPPERS_H_
