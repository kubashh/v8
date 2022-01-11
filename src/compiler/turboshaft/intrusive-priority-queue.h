// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_INTRUSIVE_PRIORITY_QUEUE_H_
#define V8_COMPILER_TURBOSHAFT_INTRUSIVE_PRIORITY_QUEUE_H_

#include "src/base/iterator.h"
#include "src/zone/zone-containers.h"

namespace v8 {
namespace internal {
namespace compiler {
namespace turboshaft {

class IntrusivePriorityQueue {
 public:
  using Priority = uint32_t;
  using Position = size_t;
  static constexpr size_t kInvalidPosition = 0;
  class Item {
   private:
    friend IntrusivePriorityQueue;
    Position position = kInvalidPosition;
  };

  struct MinIterator : base::iterator<std::forward_iterator_tag, Item*> {
    const IntrusivePriorityQueue& queue;
    Position pos;
    Priority min_priority;

    MinIterator(const IntrusivePriorityQueue& queue, Position pos,
                Priority min_priority)
        : queue(queue), pos(pos), min_priority(min_priority) {}

    Item* operator*() { return queue.heap_[pos].first; }
    void operator++() {
      Position left = LeftChild(pos);
      if (left < queue.heap_.size() &&
          queue.heap_[left].second >= min_priority) {
        pos = left;
        return;
      }
      Position right = RightChild(pos);
      if (right < queue.heap_.size() &&
          queue.heap_[right].second >= min_priority) {
        pos = right;
        return;
      }
      Position old_pos;
      do {
        if (pos == 1) {
          pos = queue.heap_.size();
          return;
        }
        old_pos = pos;
        pos = Parent(pos);
        right = RightChild(pos);
      } while (right >= queue.heap_.size() || right == old_pos ||
               queue.heap_[right].second < min_priority);
      pos = right;
    }
    bool operator!=(MinIterator other) {
      DCHECK_EQ(&queue, &other.queue);
      return pos != other.pos;
    }
  };

  struct Iterator : base::iterator<std::forward_iterator_tag, Item*> {
    std::pair<Item*, Priority>* ptr;

    explicit Iterator(std::pair<Item*, Priority>* ptr) : ptr(ptr) {}
    Item* operator*() { return ptr->first; }
    void operator++() { ++ptr; }
    bool operator!=(Iterator other) { return ptr != other.ptr; }
  };

  bool Empty() const {
    DCHECK_GE(heap_.size(), 1);
    return heap_.size() == 1;
  }

  base::iterator_range<MinIterator> MinRange(Priority min_priority) {
    Position start =
        (Empty() || heap_[1].second < min_priority) ? heap_.size() : 1;
    return {MinIterator{*this, start, min_priority},
            MinIterator{*this, heap_.size(), min_priority}};
  }

  Iterator begin() { return Iterator(heap_.data() + 1); }
  Iterator end() { return Iterator(heap_.data() + heap_.size()); }

  explicit IntrusivePriorityQueue(Zone* zone) : heap_(1, zone) {}

  void Add(Item* item, Priority priority) {
    DCHECK(item->position == kInvalidPosition);
    heap_.emplace_back(item, priority);
    SiftUp(heap_.size() - 1);
  }

  void Update(Item* item, Priority priority) {
    Position pos = item->position;
    DCHECK(pos != kInvalidPosition);
    Priority old_priority = heap_[pos].second;
    heap_[pos].second = priority;
    if (priority > old_priority) {
      SiftUp(pos);
    } else {
      SiftDown(pos);
    }
  }

  void AddOrUpdate(Item* item, Priority priority) {
    if (item->position == kInvalidPosition) {
      Add(item, priority);
    } else {
      Update(item, priority);
    }
  }

  void Remove(Item* item) {
    Position pos = item->position;
    if (pos == kInvalidPosition) return;
    if (pos != heap_.size() - 1) {
      Priority priority = heap_[pos].second;
      heap_[pos] = heap_.back();
      heap_.pop_back();
      if (heap_[pos].second > priority) {
        SiftUp(pos);
      } else {
        SiftDown(pos);
      }
    } else {
      heap_.pop_back();
    }
  }

 private:
  static Position Parent(Position pos) {
    DCHECK_GT(pos, 1);
    return pos / 2;
  }
  static Position LeftChild(Position pos) {
    DCHECK(pos != kInvalidPosition);
    return pos * 2 + 0;
  }
  static Position RightChild(Position pos) {
    DCHECK(pos != kInvalidPosition);
    return pos * 2 + 1;
  }
  void SiftUp(Position pos) {
    Priority priority = heap_[pos].second;
    while (pos > 1) {
      Position parent = Parent(pos);
      if (heap_[parent].second >= priority) break;
      std::swap(heap_[pos], heap_[parent]);
      pos = parent;
    }
  }
  void SiftDown(Position pos) {
    Priority priority = heap_[pos].second;
    while (true) {
      Position left = LeftChild(pos);
      Position right = RightChild(pos);
      if (left >= heap_.size()) return;
      Position max_child = left;
      if (right < heap_.size() && heap_[right].second > heap_[left].second) {
        max_child = right;
      }
      if (heap_[max_child].second <= priority) return;
      std::swap(heap_[max_child], heap_[pos]);
      pos = max_child;
    }
  }

  ZoneVector<std::pair<Item*, Priority>> heap_;
};

template <class T>
class IntrusivePriorityQueueTempl : IntrusivePriorityQueue {
 public:
  STATIC_ASSERT((std::is_base_of<IntrusivePriorityQueue::Item, T>::value));

  template <class BaseIterator>
  struct IteratorAdapter : base::iterator<std::forward_iterator_tag, T*> {
    BaseIterator it;
    explicit IteratorAdapter(BaseIterator it) : it(it) {}
    T* operator*() { return static_cast<T*>(*it); }
    void operator++() { ++it; }
    bool operator!=(IteratorAdapter other) { return it != other.it; }
  };

  using Iterator = IteratorAdapter<IntrusivePriorityQueue::Iterator>;
  using MinIterator = IteratorAdapter<IntrusivePriorityQueue::MinIterator>;

  using IntrusivePriorityQueue::IntrusivePriorityQueue;
  void Remove(T* item) { IntrusivePriorityQueue::Remove(item); }
  void AddOrUpdate(T* item, Priority priority) {
    IntrusivePriorityQueue::AddOrUpdate(item, priority);
  }
  void Update(T* item, Priority priority) {
    IntrusivePriorityQueue::Update(item, priority);
  }
  void Add(T* item, Priority priority) {
    IntrusivePriorityQueue::Add(item, priority);
  }
  base::iterator_range<MinIterator> MinRange(Priority min_priority) {
    auto range = IntrusivePriorityQueue::MinRange(min_priority);
    return {MinIterator(range.begin()), MinIterator(range.end())};
  }
  Iterator begin() { return Iterator(IntrusivePriorityQueue::begin()); }
  Iterator end() { return Iterator(IntrusivePriorityQueue::end()); }
};

}  // namespace turboshaft
}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_TURBOSHAFT_INTRUSIVE_PRIORITY_QUEUE_H_
