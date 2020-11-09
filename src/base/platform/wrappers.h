// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BASE_PLATFORM_WRAPPERS_H_
#define V8_BASE_PLATFORM_WRAPPERS_H_

#include <stddef.h>
#include <stdio.h>

namespace v8 {
namespace base {

V8_BASE_EXPORT void* Malloc(size_t size);

V8_BASE_EXPORT void* Realloc(V8_BASE_EXPORT void* memory, size_t size);

V8_BASE_EXPORT void Free(V8_BASE_EXPORT void* memory);

V8_BASE_EXPORT void* Calloc(size_t count, size_t size);

V8_BASE_EXPORT void* Memcpy(V8_BASE_EXPORT void* dest,
                            const V8_BASE_EXPORT void* source, size_t count);

V8_BASE_EXPORT FILE* Fopen(const char* filename, const char* mode);

V8_BASE_EXPORT int Fclose(FILE* stream);

}  // namespace base
}  // namespace v8

#endif  // V8_BASE_PLATFORM_WRAPPERS_H_
