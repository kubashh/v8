// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_LIST_INL_H_
#define V8_HEAP_LIST_INL_H_

#include "src/heap/code-range.h"
#include "src/heap/list.h"

namespace v8 {
namespace internal {
namespace heap {

template <class T>
void List<T>::AddFirstElement(T* element) {
  DCHECK(!back_);
  DCHECK(!front_);
  DCHECK(!element->list_node().next());
  DCHECK(!element->list_node().prev());
  element->AsCodePointer()->list_node().set_prev(nullptr);
  element->AsCodePointer()->list_node().set_next(nullptr);
  front_ = element;
  back_ = element;
}

template <class T>
void List<T>::InsertAfter(T* element, T* other) {
  T* other_next = other->list_node().next();
  element->AsCodePointer()->list_node().set_next(other_next);
  element->AsCodePointer()->list_node().set_prev(other);

  other->AsCodePointer()->list_node().set_next(element);
  if (other_next) {
    other_next->AsCodePointer()->list_node().set_prev(element);
  } else {
    back_ = element;
  }
}

template <class T>
void List<T>::InsertBefore(T* element, T* other) {
  T* other_prev = other->list_node().prev();
  element->list_node().set_next(other);
  element->list_node().set_prev(other_prev);
  other->list_node().set_prev(element);
  if (other_prev) {
    other_prev->list_node().set_next(element);
  } else {
    front_ = element;
  }
}

template <class T>
void List<T>::Remove(T* element) {
  DCHECK(Contains(element));
  if (back_ == element) {
    back_ = element->list_node().prev();
  }
  if (front_ == element) {
    front_ = element->list_node().next();
  }
  T* next = element->list_node().next();
  T* prev = element->list_node().prev();
  if (next) next->AsCodePointer()->list_node().set_prev(prev);
  if (prev) prev->AsCodePointer()->list_node().set_next(next);
  element->AsCodePointer()->list_node().set_prev(nullptr);
  element->AsCodePointer()->list_node().set_next(nullptr);
}

}  // namespace heap
}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_LIST_INL_H_
