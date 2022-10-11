// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/cppgc/internal/member-storage.h"

#include "include/cppgc/garbage-collected.h"
#include "include/cppgc/member.h"
#include "src/base/compiler-specific.h"
#include "src/base/macros.h"

namespace cppgc {
namespace internal {

#if defined(CPPGC_POINTER_COMPRESSION)
uintptr_t CageBaseGlobal::g_base_ = CageBaseGlobal::kLowerHalfWordMask;
#endif  // defined(CPPGC_POINTER_COMPRESSION)

// Debugging helpers.
class V8_EXPORT_PRIVATE GCedDummy : public GarbageCollected<GCedDummy> {
  void Trace(Visitor*) const {}
};

#if defined(CPPGC_POINTER_COMPRESSION)
extern "C" V8_DONT_STRIP_SYMBOL V8_EXPORT_PRIVATE void*
_cppgc_internal_Decompress_Compressed_Pointer(uint32_t cmprsd) {
  return MemberStorage::Decompress(cmprsd);
}
#endif  // !defined(CPPGC_POINTER_COMPRESSION)

extern "C" V8_DONT_STRIP_SYMBOL V8_EXPORT_PRIVATE void*
_cppgc_internal_Print_Member(Member<GCedDummy>* m) {
  return m->Get();
}

}  // namespace internal
}  // namespace cppgc
