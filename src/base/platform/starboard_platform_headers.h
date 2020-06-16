// Copyright 2020 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// This file is designed to be included in V8 header files and source files to
// provide a good coverage of Starboard interface.

#if defiend(V8_OS_STARBOARD)
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
