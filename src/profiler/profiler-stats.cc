// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/profiler/profiler-stats.h"

#include <algorithm>

#include "src/base/platform/platform.h"

namespace v8 {
namespace internal {

void ProfilerStats::AddReason(Reason reason) { counts_[reason]++; }

void ProfilerStats::Clear() {
  std::fill(counts_, counts_ + Reason::kNumberOfReasons, 0);
}

void ProfilerStats::Print() const {
  base::OS::Print("ProfilerStats:\n");
  for (int i = 0; i < Reason::kNumberOfReasons; i++) {
    base::OS::Print("  %-30s\t\t %d\n", ReasonToString(static_cast<Reason>(i)),
                    counts_[i]);
  }
}

// static
const char* ProfilerStats::ReasonToString(Reason reason) {
  switch (reason) {
    case kTickBufferFull:
      return "kTickBufferFull";
    case kSimulatorFillRegistersFailed:
      return "kSimulatorFillRegistersFailed";
    case kNoFrameRegion:
      return "kNoFrameRegion";
    case kInCallOrApply:
      return "kInCallOrApply";
    case kNoSymbolizedFrames:
      return "kNoSymbolizedFrames";
    case kNullPC:
      return "kNullPC";
    case kNumberOfReasons:
      return "kNumberOfReasons";
  }
}

}  // namespace internal
}  // namespace v8
