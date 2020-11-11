// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef V8_LOGGING_H
#define V8_LOGGING_H

#include "v8-source-location.h" // NOLINT(build/include_directory)
#include "v8config.h"           // NOLINT(build/include_directory)

[[noreturn]] void V8_EXPORT FatalImpl(
    const char*, const v8::SourceLocation& = v8::SourceLocation::Current());

#define _UNREACHABLE() FatalImpl("Unreachable code");

#endif
