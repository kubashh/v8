// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/cppgc/heap-space.h"

#include <algorithm>

#include "src/base/logging.h"
#include "src/base/platform/mutex.h"
#include "src/heap/cppgc/heap-page.h"
#include "src/heap/cppgc/object-start-bitmap.h"

namespace cppgc {
namespace internal {

BaseSpace::BaseSpace(RawHeap* heap, size_t index, PageType type,
                     bool is_compactable,
                     bool needs_lsan_contiguous_container_annotations)
    : heap_(heap),
      index_(index),
      type_(type),
      is_compactable_(is_compactable),
      needs_lsan_contiguous_container_annotations_(
          needs_lsan_contiguous_container_annotations) {}

void BaseSpace::AddPage(BasePage* page) {
  v8::base::LockGuard<v8::base::Mutex> lock(&pages_mutex_);
  DCHECK_EQ(pages_.cend(), std::find(pages_.cbegin(), pages_.cend(), page));
  pages_.push_back(page);
}

void BaseSpace::RemovePage(BasePage* page) {
  v8::base::LockGuard<v8::base::Mutex> lock(&pages_mutex_);
  auto it = std::find(pages_.cbegin(), pages_.cend(), page);
  DCHECK_NE(pages_.cend(), it);
  pages_.erase(it);
}

BaseSpace::Pages BaseSpace::RemoveAllPages() {
  Pages pages = std::move(pages_);
  pages_.clear();
  return pages;
}

NormalPageSpace::NormalPageSpace(
    RawHeap* heap, size_t index, bool is_compactable,
    bool needs_lsan_contiguous_container_annotations)
    : BaseSpace(heap, index, PageType::kNormal, is_compactable,
                needs_lsan_contiguous_container_annotations) {}

LargePageSpace::LargePageSpace(RawHeap* heap, size_t index)
    : BaseSpace(heap, index, PageType::kLarge, false /* is_compactable */,
                false /*needs_lsan_contiguous_container_annotations */) {}

}  // namespace internal
}  // namespace cppgc
