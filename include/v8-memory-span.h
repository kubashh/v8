// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INCLUDE_V8_MEMORY_SPAN_H_
#define INCLUDE_V8_MEMORY_SPAN_H_

#include <stddef.h>

#include <iterator>
#include <type_traits>

#include "v8config.h"  // NOLINT(build/include_directory)

namespace v8 {

/**
 * Points to an unowned continous buffer holding a known number of elements.
 *
 * This is similar to std::span (under consideration for C++20), but does not
 * require advanced C++ support. In the (far) future, this may be replaced with
 * or aliased to std::span.
 *
 * To facilitate future migration, this class exposes a subset of the interface
 * implemented by std::span.
 */
template <typename T>
class V8_EXPORT MemorySpan {
 public:
  /** The default constructor creates an empty span. */
  constexpr MemorySpan() = default;
  constexpr MemorySpan(const MemorySpan& ms) = default;

  constexpr MemorySpan(T* data, size_t size) : data_(data), size_(size) {}

  // Implicit conversion from standard containers supporting size() and
  // data(), e.g., std::array and std::vector.
  template <
      typename C,
      typename = std::enable_if_t<std::conjunction_v<
          std::is_convertible<decltype(std::declval<C>().data()), T*>,
          std::is_convertible<decltype(std::declval<C>().size()), size_t>>>>
  MemorySpan(C& c)  // NOLINT(runtime/explicit)
      : data_(c.data()), size_(c.size()) {}

  /** Returns a pointer to the beginning of the buffer. */
  constexpr T* data() const { return data_; }
  /** Returns the number of elements that the buffer holds. */
  constexpr size_t size() const { return size_; }

  constexpr T& operator[](size_t i) const { return data_[i]; }

  MemorySpan& operator=(const MemorySpan& ms) = default;

  class Iterator {
   public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = T;
    using difference_type = std::ptrdiff_t;
    using pointer = value_type*;
    using reference = value_type&;

    Iterator() = default;
    Iterator(const Iterator& it) = default;

    T& operator*() const { return *ptr_; }
    T* operator->() const { return ptr_; }

    bool operator==(Iterator other) const { return ptr_ == other.ptr_; }
    bool operator!=(Iterator other) const { return !(*this == other); }

    Iterator& operator=(const Iterator& it) = default;

    Iterator& operator++() {
      ++ptr_;
      return *this;
    }

    Iterator operator++(int) {
      Iterator temp(*this);
      ++(*this);
      return temp;
    }

   private:
    explicit Iterator(T* ptr) : ptr_(ptr) {}

    T* ptr_ = nullptr;
  };

  Iterator begin() const { return Iterator(data_); }
  Iterator end() const { return Iterator(data_ + size_); }

 private:
  T* data_ = nullptr;
  size_t size_ = 0;
};

}  // namespace v8
#endif  // INCLUDE_V8_MEMORY_SPAN_H_
