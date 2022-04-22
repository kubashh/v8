// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BASE_ITERATOR_H_
#define V8_BASE_ITERATOR_H_

#include <iterator>

namespace v8 {
namespace base {

template <class Category, class Type, class Diff = std::ptrdiff_t,
          class Pointer = Type*, class Reference = Type&>
struct iterator {
  using iterator_category = Category;
  using value_type = Type;
  using difference_type = Diff;
  using pointer = Pointer;
  using reference = Reference;
};

// The intention of the base::iterator_range class is to encapsulate two
// iterators so that the range defined by the iterators can be used like
// a regular STL container (actually only a subset of the full container
// functionality is available usually).
template <typename ForwardIterator>
class iterator_range {
 public:
  using iterator = ForwardIterator;
  using const_iterator = ForwardIterator;
  using pointer = typename std::iterator_traits<iterator>::pointer;
  using reference = typename std::iterator_traits<iterator>::reference;
  using value_type = typename std::iterator_traits<iterator>::value_type;
  using difference_type =
      typename std::iterator_traits<iterator>::difference_type;

  iterator_range() : begin_(), end_() {}
  iterator_range(ForwardIterator begin, ForwardIterator end)
      : begin_(begin), end_(end) {}

  iterator begin() { return begin_; }
  iterator end() { return end_; }
  const_iterator begin() const { return begin_; }
  const_iterator end() const { return end_; }
  const_iterator cbegin() const { return begin_; }
  const_iterator cend() const { return end_; }

  bool empty() const { return cbegin() == cend(); }

  // Random Access iterators only.
  reference operator[](difference_type n) { return begin()[n]; }
  difference_type size() const { return cend() - cbegin(); }

 private:
  const_iterator const begin_;
  const_iterator const end_;
};

template <typename ForwardIterator>
auto make_iterator_range(ForwardIterator begin, ForwardIterator end) {
  return iterator_range<ForwardIterator>{begin, end};
}

// {Reversed} returns a container adapter usable in a range-based "for"
// statement for iterating a reversible container in reverse order.
//
// Example:
//
//   std::vector<int> v = ...;
//   for (int i : base::Reversed(v)) {
//     // iterates through v from back to front
//   }
template <typename T>
auto Reversed(T& t) {  // NOLINT(runtime/references): match {rbegin} and {rend}
  return make_iterator_range(std::rbegin(t), std::rend(t));
}

// Canonical sentinel struct for iterators which rely on only internal state to
// decide whether to finish iteration.
//
// Expected use is having `Iterable::end()` return `IterationEndSentinel()`,
// and implementing `bool Iterator::operator!=(IterationEndSentinel)`.
struct IterationEndSentinel {};

// Helper for creating an iterable (something with begin/end methods that can
// e.g. be passed into a ranged-for loop) from a function that returns a single
// iterator.
//
// The returned iterator will call the function for its begin() method, and
// return an IterationEndSentinel for the end(). The iterator is expected to
// implement a comparison against IterationEndSentinel as its termination.
template <typename Function>
class IterableFromIteratorFactory {
 public:
  explicit IterableFromIteratorFactory(Function&& func) : func_(func) {}

  auto begin() { return func_(); }

  IterationEndSentinel end() { return IterationEndSentinel(); }

 private:
  Function func_;
};

struct IterableDereferenceTag {};
struct IterableConditionTag {};
struct IterableNextTag {};

// Creates an iterator from a function overload set that specifies the
// dereference, condition check and next value operations of iteration.
// These operations are selected by tagged dispatch, i.e. the function is called
// with different tags depending on the operation.
template <typename FunctionSet>
class IteratorFromFunctionSet
    : public iterator<std::forward_iterator_tag,
                      // Infer the iterator's value type from the call with
                      // IterableDereferenceTag.
                      std::decay<decltype(std::declval<FunctionSet>()(
                          IterableDereferenceTag()))>> {
 public:
  explicit IteratorFromFunctionSet(FunctionSet&& func) : func_(func) {}

  // Use decltype(auto) instead of auto so that operator*() can return a
  // reference.
  decltype(auto) operator*() { return func_(IterableDereferenceTag{}); }

  bool operator!=(IterationEndSentinel) const {
    // Ugly casting away of const to allow the lambda + if constexpr hackery
    // below, which can change return types but can't force the function to
    // be sometimes const and sometimes not.
    return const_cast<IteratorFromFunctionSet*>(this)->func_(
        IterableConditionTag{});
  }

  IteratorFromFunctionSet& operator++() {
    func_(IterableNextTag{});
    return *this;
  }

  IteratorFromFunctionSet operator++(int) {
    IteratorFromFunctionSet copy(func_);
    ++*this;
    return copy;
  }

 private:
  FunctionSet func_;
};

// Create an iterator from the given derefence, condition, and next value
// expressions.
//
// This can be combined with IterableFromIteratorFactory to easily create
// simple iterators. Expected use would be something like:
//
//     class Foo {
//       public:
//         auto values() {
//             // Use [=] to capture the this pointer by value.
//             return IterableFromIteratorFactory([=](){
//                 int* x = values_;
//                 // MAKE_ITERATOR captures x by mutable copy.
//                 return MAKE_ITERATOR(*x, x != 0, x++);
//             });
//         }
//       private:
//         int* values_;
//     };
//
//     void iterate_values(Foo& foo) {
//         for (int x : foo.values()) {
//             print(x);
//         }
//     }
//
// Under the hood, this creates a single lambda which captures all its locals by
// mutable copy. Then, it takes the IteratorFromFunctionSet tag as the input,
// and uses `if constexpr` to dispatch on the tag. The combined magic of
// auto-parameter lambdas and `if constexpr` means that these paths can have
// different return value types and the correct type is inferred.
#define MAKE_ITERATOR(DEREFERENCE, CONDITION, ...)                           \
  ::v8::base::IteratorFromFunctionSet([=](auto tag) mutable -> decltype(     \
                                                                auto) {      \
    if constexpr (std::is_same_v<decltype(tag),                              \
                                 ::v8::base::IterableDereferenceTag>) {      \
      return (DEREFERENCE);                                                  \
      /* NOLINTNEXTLINE */                                                   \
    } else if constexpr (std::is_same_v<decltype(tag),                       \
                                        ::v8::base::IterableConditionTag>) { \
      return (CONDITION);                                                    \
      /* NOLINTNEXTLINE */                                                   \
    } else if constexpr (std::is_same_v<decltype(tag),                       \
                                        ::v8::base::IterableNextTag>) {      \
      __VA_ARGS__;                                                           \
    }                                                                        \
  })

}  // namespace base
}  // namespace v8

#endif  // V8_BASE_ITERATOR_H_
