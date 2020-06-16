// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file is designed to be included in V8 header files and source files to
// provide a good coverage of Starboard interface includes.

#ifndef V8_BASE_PLATFORM_STARBOARD_PLATFORM_HEADERS_H_
#define V8_BASE_PLATFORM_STARBOARD_PLATFORM_HEADERS_H_

#if defined(V8_OS_STARBOARD)
#include "starboard/common/condition_variable.h"
#include "starboard/common/log.h"
#include "starboard/common/string.h"
#include "starboard/configuration.h"
#include "starboard/configuration_constants.h"
#include "starboard/log.h"
#include "starboard/memory.h"
#include "starboard/mutex.h"
#include "starboard/string.h"
#include "starboard/time.h"
#include "starboard/time_zone.h"
#endif  // V8_OS_STARBOARD

#endif // V8_BASE_PLATFORM_STARBOARD_PLATFORM_HEADERS_H_
