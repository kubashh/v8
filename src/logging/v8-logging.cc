
// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/v8-logging.h"

#include "include/v8-source-location.h"
#include "src/base/logging.h"

void FatalImpl(const char* message, const v8::SourceLocation& loc) {
#if DEBUG
  V8_Fatal(loc.FileName(), static_cast<int>(loc.Line()), "%s", message);
#elif !defined(OFFICIAL_BUILD)
  V8_Fatal("%s", message);
#else
  V8_Fatal("ignored");
#endif
}
