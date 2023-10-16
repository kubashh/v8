// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INCLUDE_V8_ITERATOR_H_
#define INCLUDE_V8_ITERATOR_H_

#include <iterator>
#include <type_traits>

namespace v8 {

// A class of iterators that wrap some different iterator type.
// If specified, ElementType is the type of element accessed by the wrapper
// iterator; in this case, the actual reference and pointer types of Iterator
// must be convertible to ElementType& and ElementType*, respectively.
template <typename Iterator, typename ElementType = void>
class WrappedIterator {
 public:
  static_assert(
      !std::is_void_v<ElementType> ||
      (std::is_convertible_v<typename std::iterator_traits<Iterator>::pointer,
                             ElementType*> &&
       std::is_convertible_v<typename std::iterator_traits<Iterator>::reference,
                             ElementType&>));

  using iterator_category =
      typename std::iterator_traits<Iterator>::iterator_category;
  using difference_type =
      typename std::iterator_traits<Iterator>::difference_type;
  using value_type =
      std::conditional_t<std::is_void_v<ElementType>,
                         typename std::iterator_traits<Iterator>::value_type,
                         ElementType>;
  using pointer =
      std::conditional_t<std::is_void_v<ElementType>,
                         typename std::iterator_traits<Iterator>::pointer,
                         ElementType*>;
  using reference =
      std::conditional_t<std::is_void_v<ElementType>,
                         typename std::iterator_traits<Iterator>::reference,
                         ElementType&>;

  constexpr WrappedIterator() noexcept : it_() {}
  constexpr explicit WrappedIterator(Iterator it) noexcept : it_(it) {}

  template <typename OtherIterator, typename OtherElementType,
            std::enable_if_t<std::is_convertible_v<OtherIterator, Iterator>,
                             bool> = true>
  constexpr WrappedIterator(
      const WrappedIterator<OtherIterator, OtherElementType>& it) noexcept
      : it_(it.base()) {}

  constexpr reference operator*() const noexcept { return *it_; }
  constexpr pointer operator->() const noexcept { return it_.operator->(); }

  constexpr WrappedIterator& operator++() noexcept {
    ++it_;
    return *this;
  }
  constexpr WrappedIterator operator++(int) noexcept {
    WrappedIterator result(*this);
    ++(*this);
    return result;
  }

  constexpr WrappedIterator& operator--() noexcept {
    --it_;
    return *this;
  }
  constexpr WrappedIterator operator--(int) noexcept {
    WrappedIterator result(*this);
    --(*this);
    return result;
  }
  constexpr WrappedIterator operator+(difference_type n) const noexcept {
    WrappedIterator result(*this);
    result += n;
    return result;
  }
  constexpr WrappedIterator& operator+=(difference_type n) noexcept {
    it_ += n;
    return *this;
  }
  constexpr WrappedIterator operator-(difference_type n) const noexcept {
    return *this + (-n);
  }
  constexpr WrappedIterator& operator-=(difference_type n) noexcept {
    *this += -n;
    return *this;
  }
  constexpr reference operator[](difference_type n) const noexcept {
    return it_[n];
  }

  constexpr Iterator base() const noexcept { return it_; }

 private:
  template <typename OtherIterator, typename OtherElementType>
  friend class WrappedIterator;

 private:
  Iterator it_;
};

template <typename Iterator, class ElementType, class OtherIterator,
          typename OtherElementType>
constexpr bool operator==(
    const WrappedIterator<Iterator, ElementType>& x,
    const WrappedIterator<OtherIterator, OtherElementType>& y) noexcept {
  return x.base() == y.base();
}

template <typename Iterator, class ElementType, class OtherIterator,
          typename OtherElementType>
constexpr bool operator<(
    const WrappedIterator<Iterator, ElementType>& x,
    const WrappedIterator<OtherIterator, OtherElementType>& y) noexcept {
  return x.base() < y.base();
}

template <typename Iterator, class ElementType, class OtherIterator,
          typename OtherElementType>
constexpr bool operator!=(
    const WrappedIterator<Iterator, ElementType>& x,
    const WrappedIterator<OtherIterator, OtherElementType>& y) noexcept {
  return !(x == y);
}

template <typename Iterator, class ElementType, class OtherIterator,
          typename OtherElementType>
constexpr bool operator>(
    const WrappedIterator<Iterator, ElementType>& x,
    const WrappedIterator<OtherIterator, OtherElementType>& y) noexcept {
  return y < x;
}

template <typename Iterator, class ElementType, class OtherIterator,
          typename OtherElementType>
constexpr bool operator>=(
    const WrappedIterator<Iterator, ElementType>& x,
    const WrappedIterator<OtherIterator, OtherElementType>& y) noexcept {
  return !(x < y);
}

template <typename Iterator, class ElementType, class OtherIterator,
          typename OtherElementType>
constexpr bool operator<=(
    const WrappedIterator<Iterator, ElementType>& x,
    const WrappedIterator<OtherIterator, OtherElementType>& y) noexcept {
  return !(y < x);
}

template <typename Iterator, class ElementType, class OtherIterator,
          typename OtherElementType>
constexpr auto operator-(
    const WrappedIterator<Iterator, ElementType>& x,
    const WrappedIterator<OtherIterator, OtherElementType>& y) noexcept
    -> decltype(x.base() - y.base()) {
  return x.base() - y.base();
}

template <typename Iterator, typename ElementType>
constexpr WrappedIterator<Iterator> operator+(
    typename WrappedIterator<Iterator, ElementType>::difference_type n,
    const WrappedIterator<Iterator, ElementType>& x) noexcept {
  x += n;
  return x;
}

}  // namespace v8

#endif  // INCLUDE_V8_ITERATOR_H_
