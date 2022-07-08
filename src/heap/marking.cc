// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/marking.h"

#include <cstdint>

#include "src/heap/memory-chunk.h"

namespace v8 {
namespace internal {

const size_t Bitmap::kSize = Bitmap::CellsCount() * Bitmap::kBytesPerCell;

template <>
bool ConcurrentBitmap<AccessMode::NON_ATOMIC>::AllBitsSetInRange(
    uint32_t start_index, uint32_t end_index) {
  if (start_index >= end_index) return false;
  end_index--;

  unsigned int start_cell_index = start_index >> Bitmap::kBitsPerCellLog2;
  MarkBit::CellType start_index_mask = 1u << Bitmap::IndexInCell(start_index);

  unsigned int end_cell_index = end_index >> Bitmap::kBitsPerCellLog2;
  MarkBit::CellType end_index_mask = 1u << Bitmap::IndexInCell(end_index);

  MarkBit::CellType matching_mask;
  if (start_cell_index != end_cell_index) {
    matching_mask = ~(start_index_mask - 1);
    if ((cells()[start_cell_index] & matching_mask) != matching_mask) {
      return false;
    }
    for (unsigned int i = start_cell_index + 1; i < end_cell_index; i++) {
      if (cells()[i] != ~0u) return false;
    }
    matching_mask = end_index_mask | (end_index_mask - 1);
    return ((cells()[end_cell_index] & matching_mask) == matching_mask);
  } else {
    matching_mask = end_index_mask | (end_index_mask - start_index_mask);
    return (cells()[end_cell_index] & matching_mask) == matching_mask;
  }
}

template <>
bool ConcurrentBitmap<AccessMode::NON_ATOMIC>::AllBitsClearInRange(
    uint32_t start_index, uint32_t end_index) {
  if (start_index >= end_index) return true;
  end_index--;

  unsigned int start_cell_index = start_index >> Bitmap::kBitsPerCellLog2;
  MarkBit::CellType start_index_mask = 1u << Bitmap::IndexInCell(start_index);

  unsigned int end_cell_index = end_index >> Bitmap::kBitsPerCellLog2;
  MarkBit::CellType end_index_mask = 1u << Bitmap::IndexInCell(end_index);

  MarkBit::CellType matching_mask;
  if (start_cell_index != end_cell_index) {
    matching_mask = ~(start_index_mask - 1);
    if ((cells()[start_cell_index] & matching_mask)) return false;
    for (unsigned int i = start_cell_index + 1; i < end_cell_index; i++) {
      if (cells()[i]) return false;
    }
    matching_mask = end_index_mask | (end_index_mask - 1);
    return !(cells()[end_cell_index] & matching_mask);
  } else {
    matching_mask = end_index_mask | (end_index_mask - start_index_mask);
    return !(cells()[end_cell_index] & matching_mask);
  }
}

namespace {

void PrintWord(uint32_t word, uint32_t himask = 0) {
  for (uint32_t mask = 1; mask != 0; mask <<= 1) {
    if ((mask & himask) != 0) PrintF("[");
    PrintF((mask & word) ? "1" : "0");
    if ((mask & himask) != 0) PrintF("]");
  }
}

class CellPrinter {
 public:
  CellPrinter() : seq_start(0), seq_type(0), seq_length(0) {}

  void Print(size_t pos, uint32_t cell) {
    if (cell == seq_type) {
      seq_length++;
      return;
    }

    Flush();

    if (IsSeq(cell)) {
      seq_start = pos;
      seq_length = 0;
      seq_type = cell;
      return;
    }

    PrintF("%zu: ", pos);
    PrintWord(cell);
    PrintF("\n");
  }

  void Flush() {
    if (seq_length > 0) {
      PrintF("%zu: %dx%zu\n", seq_start, seq_type == 0 ? 0 : 1,
             seq_length * Bitmap::kBitsPerCell);
      seq_length = 0;
    }
  }

  static bool IsSeq(uint32_t cell) { return cell == 0 || cell == 0xFFFFFFFF; }

 private:
  size_t seq_start;
  uint32_t seq_type;
  size_t seq_length;
};

}  // anonymous namespace

template <>
void ConcurrentBitmap<AccessMode::NON_ATOMIC>::Print() {
  CellPrinter printer;
  for (size_t i = 0; i < CellsCount(); i++) {
    printer.Print(i, cells()[i]);
  }
  printer.Flush();
  PrintF("\n");
}

template <>
bool ConcurrentBitmap<AccessMode::NON_ATOMIC>::IsClean() {
  for (size_t i = 0; i < CellsCount(); i++) {
    if (cells()[i] != 0) {
      return false;
    }
  }
  return true;
}

template <>
Address ConcurrentBitmap<AccessMode::NON_ATOMIC>::FindPreviousMarkedObject(
    const MemoryChunk* chunk, Address maybe_inner_ptr) const {
  const MarkBit::CellType* cells = this->cells();
  uint32_t index = chunk->AddressToMarkbitIndex(maybe_inner_ptr);
  unsigned int cell_index = Bitmap::IndexToCell(index);
  MarkBit::CellType mask = 1u << Bitmap::IndexInCell(index);
  MarkBit::CellType cell = cells[cell_index];
  // If the markbit is already set, bail out.
  if ((cell & mask) != 0) return kNullAddress;
  // Clear the bits corresponding to higher addresses in the cell.
  cell &= ((~static_cast<MarkBit::CellType>(0)) >>
           (Bitmap::kBitsPerCell - Bitmap::IndexInCell(index) - 1));
  // Find the start of a valid object by traversing the bitmap backwards, until
  // we find a markbit that is set and whose previous markbit (if it exists) is
  // unset.
  // First, iterate backwards to find a cell with any set markbit.
  while (cell == 0 && cell_index > 0) cell = cells[--cell_index];
  if (cell == 0) {
    DCHECK_EQ(0, cell_index);
    // We have reached the start of the chunk.
    return chunk->MarkbitIndexToAddress(0);
  }
  // We have found such a cell.
  uint32_t leading_zeros = base::bits::CountLeadingZeros(cell);
  uint32_t leftmost_ones =
      base::bits::CountLeadingZeros(~(cell << leading_zeros));
  uint32_t index_of_last_leftmost_one =
      Bitmap::kBitsPerCell - leading_zeros - leftmost_ones;
  // If the leftmost contiguous sequence of set bits does not reach the start
  // of the cell, we found it.
  if (index_of_last_leftmost_one > 0) {
    return chunk->MarkbitIndexToAddress(cell_index * Bitmap::kBitsPerCell +
                                        index_of_last_leftmost_one);
  }
  // The leftmost contiguous sequence of set bits reaches the start of the
  // cell. We must keep traversing backwards until we find the first unset
  // markbit.
  if (cell_index == 0) {
    // We have reached the start of the chunk.
    return chunk->MarkbitIndexToAddress(0);
  }
  // Iterate backwards to find a cell with any unset markbit.
  do {
    cell = cells[--cell_index];
  } while (~cell == 0 && cell_index > 0);
  if (~cell == 0) {
    DCHECK_EQ(0, cell_index);
    // We have reached the start of the chunk.
    return chunk->MarkbitIndexToAddress(0);
  }
  // We have found such a cell.
  uint32_t leading_ones = base::bits::CountLeadingZeros(~cell);
  uint32_t index_of_last_leading_one = Bitmap::kBitsPerCell - leading_ones;
  DCHECK_LT(0, index_of_last_leading_one);
  return chunk->MarkbitIndexToAddress(cell_index * Bitmap::kBitsPerCell +
                                      index_of_last_leading_one);
}

}  // namespace internal
}  // namespace v8
