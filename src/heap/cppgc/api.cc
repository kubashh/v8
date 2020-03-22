// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/cppgc/gc-info.h"
#include "include/v8config.h"
#include "src/heap/cppgc/gc-info-table.h"

namespace cppgc {
namespace internal {

GCInfoIndex GCInfoTableProxy::EnsureGCInfoIndex(
    const GCInfo& info, std::atomic<GCInfoIndex>* index) {
  return GlobalGCInfoTable::GetMutable()->EnsureGCInfoIndex(info, index);
}

}  // namespace internal
}  // namespace cppgc
