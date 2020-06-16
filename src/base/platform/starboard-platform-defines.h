// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Specialized defines for porting V8 on top of Starboard.  It would have been
// nice to have been able to use the starboard APIs, however they are too
// aggressive for V8 (such as grabbing identifiers that will come after std::).

#ifndef V8_BASE_PLATFORM_STARBOARD_PLATFORM_DEFINES_H_
#define V8_BASE_PLATFORM_STARBOARD_PLATFORM_DEFINES_H_

#if !defined(STARBOARD)
#error "Including V8 defines without STARBOARD defined."
#endif

#if !defined(V8_OS_STARBOARD)
#error "Including V8 defines without V8_OS_STARBOARD defined."
#endif

#include "starboard/memory.h"
#include "starboard/string.h"

#define malloc(x) SbMemoryAllocate(x)
#define realloc(x, y) SbMemoryReallocate(x, y)
#define free(x) SbMemoryDeallocate(x)
#define calloc(x, y) SbMemoryCalloc(x, y)
#define strdup(s) SbStringDuplicate(s)
#define printf(x) SbLogRaw(x)
#define __builtin_abort SbSystemBreakIntoDebugger

// No-ops for now
#define fopen(x)
#define fclose(x)
#define feof(x) 1
#define fgets(x, y, z) nullptr
#define ferror(x) 1
#define fseek(x, y, z) 1
#define fread(x, y, z, q) 0
#define ftell(x) -1L
#define puts(x)
#define fputs(x)

static FILE* null_file_ptr = nullptr;
#undef stderr
#define stderr null_file_ptr
#undef stdout
#define stdout null_file_ptr
#define fflush(x)

#endif  // V8_BASE_PLATFORM_STARBOARD_PLATFORM_DEFINES_H_
