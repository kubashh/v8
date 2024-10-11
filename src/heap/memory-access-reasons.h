// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef _INCLUDE_HEAP_DUMP_MEMORY_ACCESS_REASONS_H_
#define _INCLUDE_HEAP_DUMP_MEMORY_ACCESS_REASONS_H_

#include <string_view>


namespace heap_dump {

enum class MemoryAccessReason {
   kFetchVisitorId = 1 << 0,
   kFetchInstanceDescriptorsOffset = 1 << 1,
   kFetchNumberOfOwnDescriptors = 1 << 2,
   kFetchNumberOfDescriptors = 1 << 3,
   kInobjectPropertiesStartOrConstructorFunctionIndexOffset = 1 << 4,
   kInstanceSizeInWordsOffset = 1 << 5,
   kFetchString = 1 << 6,
   kMarkingBitMap = 1 << 7,
   kFetchObject = 1 << 8,
   kFetchInstanceSize = 1 << 9,
   kJSFunctionDispatchOffset = 1 << 10,
   kObjectFirstWord =  1 << 11,
};

constexpr int kNumMemoryAccessReasons = 12;

constexpr std::string_view MemoryAccessReasonNames[] = {
  "kFetchVisitorId",
  "kFetchInstanceDescriptorsOffset",
  "kFetchNumberOfOwnDescriptors",
  "kFetchNumberOfDescriptors",
  "kInobjectPropertiesStartOrConstructorFunctionIndexOffset",
  "kInstanceSizeInWordsOffset",
  "kFetchString",
  "kMarkingBitMap",
  "kFetchObject",
  "kFetchInstanceSize",
  "kJSFunctionDispatchOffset",
  "kObjectFirstWord",
};

}
#endif /* #ifndef _INCLUDE_HEAP_DUMP_MEMORY_ACCESS_REASONS_H_ */
