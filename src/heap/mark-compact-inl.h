// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_MARK_COMPACT_INL_H_
#define V8_HEAP_MARK_COMPACT_INL_H_

#include "src/heap/mark-compact.h"
#include "src/heap/remembered-set.h"
#include "src/isolate.h"

namespace v8 {
namespace internal {

void MarkCompactCollector::PushBlack(HeapObject* obj) {
  DCHECK((ObjectMarking::IsBlack<AccessMode::NON_ATOMIC>(
      obj, MarkingState::Internal(obj))));
  if (!marking_deque()->Push(obj)) {
    ObjectMarking::BlackToGrey<AccessMode::NON_ATOMIC>(
        obj, MarkingState::Internal(obj));
  }
}

void MarkCompactCollector::UnshiftBlack(HeapObject* obj) {
  DCHECK(ObjectMarking::IsBlack(obj, MarkingState::Internal(obj)));
  if (!marking_deque()->Unshift(obj)) {
    ObjectMarking::BlackToGrey(obj, MarkingState::Internal(obj));
  }
}

void MarkCompactCollector::MarkObject(HeapObject* obj) {
  if (ObjectMarking::WhiteToBlack<AccessMode::NON_ATOMIC>(
          obj, MarkingState::Internal(obj))) {
    PushBlack(obj);
  }
}

void MarkCompactCollector::RecordSlot(HeapObject* object, Object** slot,
                                      Object* target) {
  Page* target_page = Page::FromAddress(reinterpret_cast<Address>(target));
  Page* source_page = Page::FromAddress(reinterpret_cast<Address>(object));
  if (target_page->IsEvacuationCandidate() &&
      !ShouldSkipEvacuationSlotRecording(object)) {
    DCHECK(
        ObjectMarking::IsBlackOrGrey(object, MarkingState::Internal(object)));
    RememberedSet<OLD_TO_OLD>::Insert(source_page,
                                      reinterpret_cast<Address>(slot));
  }
}

template <LiveObjectIterationMode T>
HeapObject* LiveObjectIterator<T>::Next() {
  while (!it_.Done()) {
    HeapObject* object = nullptr;
    while (current_cell_ != 0) {
      uint32_t trailing_zeros = base::bits::CountTrailingZeros32(current_cell_);
      Address addr = cell_base_ + trailing_zeros * kPointerSize;

      // Clear the first bit of the found object..
      current_cell_ &= ~(1u << trailing_zeros);

      uint32_t second_bit_index = 1u << (trailing_zeros + 1);
      if (trailing_zeros >= Bitmap::kBitIndexMask) {
        second_bit_index = 0x1;
        // The overlapping case; there has to exist a cell after the current
        // cell.
        //
        // Exception: If there if there is a black area at the end of the page
        // and the last word is a one word filler, we are not allowed to
        // advance. Return immediately in that case.
        if (!it_.Advance()) {
          DCHECK(HeapObject::FromAddress(addr)->map() == one_word_filler_map_);
          return nullptr;
        }
        cell_base_ = it_.CurrentCellBase();
        current_cell_ = *it_.CurrentCell();
      }

      object = HeapObject::FromAddress(addr);
      Map* map = object->map();
      bool second_bit_set = current_cell_ & second_bit_index;

      // Advance the iterator. One word filler objects do not borrow the
      // second mark bit. For all others we can jump over the object payload.
      // Note that for black allocated objects we actually have to advance
      // over the object payload, while for regular black or grey objects
      // this would be optional.
      if (map != one_word_filler_map_) {
        Address object_end = addr + object->SizeFromMap(map) - kPointerSize;
        DCHECK_EQ(chunk_, MemoryChunk::FromAddress(object_end));
        uint32_t end_mark_bit_index = chunk_->AddressToMarkbitIndex(object_end);
        unsigned int end_cell_index =
            end_mark_bit_index >> Bitmap::kBitsPerCellLog2;
        MarkBit::CellType end_index_mask =
            1u << Bitmap::IndexInCell(end_mark_bit_index);
        if (it_.Advance(end_cell_index)) {
          cell_base_ = it_.CurrentCellBase();
          current_cell_ = *it_.CurrentCell();
        }
        // Clear all bits in current_cell, including the end index.
        current_cell_ &= ~(end_index_mask + end_index_mask - 1);
      }

      if (T == kAllLiveObjects || (T == kBlackObjects && second_bit_set) ||
          (T == kGreyObjects && !second_bit_set)) {
        // Do not use IsFiller() here. This may cause a data race for reading
        // out the instance type when a new map concurrently is written into
        // this object while iterating over the object.
        if (map == one_word_filler_map_ || map == two_word_filler_map_ ||
            map == free_space_map_) {
          // There are two reasons why we can get black or grey fillers:
          // 1) Black areas together with slack tracking may result in black one
          // word filler objects.
          // 2) Left trimming may leave black or grey fillers behind because we
          // do not clear the old location of the object start.
          // We filter these objects out in the iterator.
          object = nullptr;
        } else {
          break;
        }
      }
    }

    if (current_cell_ == 0) {
      if (it_.Advance()) {
        cell_base_ = it_.CurrentCellBase();
        current_cell_ = *it_.CurrentCell();
      }
    }
    if (object != nullptr) return object;
  }
  return nullptr;
}

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_MARK_COMPACT_INL_H_
