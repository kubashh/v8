// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INCLUDE_CPPGC_HEAP_H_
#define INCLUDE_CPPGC_HEAP_H_

#include <memory>

#include "v8config.h"  // NOLINT(build/include_directory)

namespace cppgc {
namespace internal {
class Heap;
}  // namespace internal

template <size_t>
struct CustomSpaceTrait {
  // TODO(chromium:1056170): Provide kIsCompactable to specify whether a custom
  // space should be compacted. Such spaces must adhere to specific rules.
};

class V8_EXPORT Heap {
 public:
  /**
   * Specifies where objects are allocated. Regular users should not touch the
   * policy. Advanced users may specify the policy to encapsulate objects
   * into their own spaces.
   */
  enum class SpacePolicy : uint8_t {
    /**
     * Default policy: The garabge collector figures out placement in spaces.
     */
    kDefault,
    /**
     * Custom policy: Used together with CustomSpaceTrait and SpacePolicyTrait
     * to specify the space objects are allocated on and how to treat those
     * spaces.
     */
    kCustom,
  };

  /**
   * Specifies the stack state the embedder is in.
   */
  enum class StackState : uint8_t {
    /**
     * The embedder does not know anything about it's stack.
     */
    kUnkown,
    /**
     * The stack is empty, i.e., it does not contain any raw pointers
     * to garbage-collected objects.
     */
    kEmpty,
    /**
     * The stack is non-empty, i.e., it may contain raw pointers to
     * garabge-collected objects.
     */
    kNonEmpty,
  };

  struct HeapOptions {
    static HeapOptions Default() { return {0}; }

    size_t custom_spaces = 0;
  };

  static std::unique_ptr<Heap> Create(HeapOptions = HeapOptions::Default());

  virtual ~Heap() = default;

  /**
   * Forces garbage collection.
   *
   * \param source String specifying the source (or caller) triggering a
   *   forced garbage collection.
   * \param reason String specifying the reason for the forced garbage
   *   collection.
   * \param stack_state The embedder stack state, see StackState.
   */
  void ForceGarbageCollectionSlow(const char* source, const char* reason,
                                  StackState stack_state = StackState::kUnkown);

 private:
  Heap() = default;

  friend class internal::Heap;
};

}  // namespace cppgc

#endif  // INCLUDE_CPPGC_HEAP_H_
