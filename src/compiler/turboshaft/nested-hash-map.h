// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_NESTED_HASH_MAP_H_
#define V8_COMPILER_TURBOSHAFT_NESTED_HASH_MAP_H_

#include <type_traits>

#include "src/base/vector.h"
#include "src/compiler/turboshaft/fast-hash.h"
#include "src/zone/zone-containers.h"

namespace v8::internal::compiler::turboshaft {

template <class T>
class AlignedStorage {
 public:
  template <class... Args>
  T& Construct(Args&&... args) V8_NOEXCEPT {
    return *new (&storage_) T{std::forward<Args>(args)...};
  }
  T& get() { return *reinterpret_cast<T*>(&storage_); }
  const T& get() const { return *reinterpret_cast<const T*>(&storage_); }

 private:
  alignas(alignof(T)) char storage_[sizeof(T)];
};

// This hash table is called "nested" because for each slot with a key-value
// pair, there is a small nested hash-table. The bits from the hash value are
// split to index both the slot and then to an entry inside of the nested hash
// table. The nested hash tables are very sparse even at high load factors, so
// collisions there are very rare. Each entry in the nested hash table contains
// an offset counting how many slots to go forwards to find the actual key-value
// pair.
template <typename K, typename V, typename Hash = fast_hash<K>,
          typename KeyEqual = std::equal_to<K>>
class NestedHashMap {
  static constexpr size_t kInitialCapacity = 8;
  static constexpr size_t kInnerBits = sizeof(std::pair<K, V>) <= 8 ? 3 : 4;
  static constexpr size_t kInnerSize = 1 << kInnerBits;
  // The probe length has to be coprime to the table size to ensure we hit every
  // possible spot, which is satisfied by any odd number. In addition, we pick a
  // number with the binary representation 1010101, which has the property that
  // it produces roughly either a quarter or half of the table size when masked
  // with `mask_` and skips over roughly 128 elements, which is the maximal
  // offset.
  static constexpr size_t kProbeStep = 125;

  struct Entry {
    static constexpr size_t kMaxOffset = 126;
    static constexpr uint8_t kInvalidOffset = 127;
    uint8_t data = kInvalidOffset;
    bool valid() const { return (data & 0x7f) != kInvalidOffset; }
    bool has_collision() const { return data & 0x80; }
    size_t offset() const {
      DCHECK(valid());
      return data & 0x7f;
    }
    Entry with_collision() const {
      return Entry{static_cast<uint8_t>(data | 0x80)};
    }
    Entry with_offset(size_t offset) const {
      DCHECK(!valid());
      DCHECK_LE(offset, kMaxOffset);
      return Entry{static_cast<uint8_t>(data + offset - kInvalidOffset)};
    }
  };
  struct Slot {
    Entry inner_table[kInnerSize];
    AlignedStorage<K> key;
    AlignedStorage<V> value;
  };

 public:
  // Constructs an empty map.
  explicit NestedHashMap(Zone* zone)
      : table_(zone), free_slots_(kInitialCapacity), free_bitmap_(zone) {
    table_.resize(kInitialCapacity);
    mask_ = table_.size() - 1;
    InitFreeBitset();
  }
  V8_INLINE V& operator[](const K& key) {
    if (free_slots_ <= SlotCount() / 8) {
      Grow();
    }
    return FindOrCreateEntry(key).second;
  }

  const V* Find(const K& key) const {
    auto slot = FindEntry(key);
    if (slot) {
      return &slot->second;
    }
    return nullptr;
  }

  V* Find(const K& key) {
    auto slot = FindEntry(key);
    if (slot) {
      return &slot->second;
    }
    return nullptr;
  }

  bool Contains(const K& key) const { return FindEntry(key).has_value(); }

  template <class F>
  void ForEach(F f) {
    size_t slot_count = SlotCount();
    for (size_t i = 0; i < slot_count; ++i) {
      if (SlotIsFree(i, free_bitmap_)) continue;
      auto slot = GetSlot(i);
      f(slot.first.get(), slot.second.get());
    }
  }

 private:
  mutable ZoneVector<Slot> table_;
  size_t mask_;
  size_t free_slots_;
  ZoneVector<uint8_t> free_bitmap_;

  size_t SlotCount() const { return table_.size(); }

  void InitFreeBitset() {
    free_bitmap_.resize(table_.size() / 8 + 1 + sizeof(uintptr_t));
    size_t slot_count = SlotCount();
    for (size_t i = 0; i < slot_count / 8; ++i) {
      free_bitmap_[i] = 0xff;
    }
    if (slot_count % 8 != 0) {
      free_bitmap_[slot_count / 8] = (size_t{1} << (slot_count % 8)) - 1;
    }
  }

  std::pair<AlignedStorage<K>&, AlignedStorage<V>&> GetSlot(
      size_t slot_index) const {
    Slot& bucket = table_[slot_index];
    return {bucket.key, bucket.value};
  }

  static size_t ComputeHash(const K& key) {
    size_t hash = Hash()(key);
    // This is a simple way to mix-up the bits and spread the entropy. There is
    // nothing special about this constant, it's just a random number.
    hash = static_cast<size_t>(hash * 0xa4173ef0947c9ae9u >> 24);
    return hash;
  }

  base::Optional<std::pair<K&, V&>> FindEntry(const K& key) const {
    size_t hash = ComputeHash(key);
    size_t outer_hash = hash >> kInnerBits;

    size_t i = 0;
    while (true) {
      size_t slot_index = (outer_hash + i) & mask_;
      Slot& slot = table_[slot_index];
      Entry entry = slot.inner_table[hash & (kInnerSize - 1)];
      if (entry.valid()) {
        auto slot = GetSlot(slot_index + entry.offset());
        if (V8_LIKELY(slot.first.get() == key)) {
          return std::pair<K&, V&>{slot.first.get(), slot.second.get()};
        }
      } else if (V8_LIKELY(!entry.has_collision())) {
        return {};
      }
      i += kProbeStep;
      if ((i & mask_) != 0) break;
    }
    return {};
  }

  bool SlotIsFree(size_t slot_index, const ZoneVector<uint8_t>& bitmap) {
    return bitmap[slot_index / 8] & (1 << (slot_index % 8));
  }
  void MarkSlotAsOccupied(size_t slot_index) {
    free_bitmap_[slot_index / 8] &= ~(1 << (slot_index % 8));
    free_slots_--;
  }
  bool FindFreeSlot(size_t slot_index, uint8_t* result) const {
    uintptr_t bits;
    std::memcpy(&bits, &free_bitmap_[slot_index / 8], sizeof(uintptr_t));
    bits >>= slot_index % 8;
    if (V8_LIKELY(bits != 0)) {
      *result = base::bits::CountTrailingZeros(bits);
      return true;
    }
    return false;
  }

  template <class... ValueArgs>
  V8_INLINE std::pair<K&, V&> FindOrCreateEntry(const K& key,
                                                ValueArgs&&... value_args) {
    size_t hash = ComputeHash(key);
    size_t outer_hash = hash >> kInnerBits;

    size_t i = 0;
    while (true) {
      size_t bucket_index = (outer_hash + i) & mask_;
      Slot& bucket = table_[bucket_index];
      Entry& entry = bucket.inner_table[hash & (kInnerSize - 1)];
      if (entry.valid()) {
        auto slot = GetSlot(bucket_index + entry.offset());
        if (V8_LIKELY(slot.first.get() == key)) {
          return {slot.first.get(), slot.second.get()};
        }
      } else if (uint8_t offset;
                 V8_LIKELY(FindFreeSlot(bucket_index, &offset))) {
        entry = entry.with_offset(offset);
        size_t slot_index = bucket_index + offset;
        MarkSlotAsOccupied(slot_index);
        auto slot = GetSlot(slot_index);
        return {slot.first.Construct(key),
                slot.second.Construct(std::forward<ValueArgs>(value_args)...)};
      }
      entry = entry.with_collision();
      i += kProbeStep;
      DCHECK_NE(i & mask_, 0);
    }
  }

  void Grow() {
    size_t old_size = table_.size();
    size_t new_size = 2 * old_size;
    ZoneVector<Slot> old_table = std::move(table_);
    ZoneVector<uint8_t> old_free_bitmap = std::move(free_bitmap_);
    table_.clear();
    table_.resize(new_size);
    mask_ = new_size - 1;
    free_slots_ = SlotCount();
    InitFreeBitset();
    for (size_t bucket_index = 0; bucket_index < old_table.size();
         ++bucket_index) {
      Slot& bucket = old_table[bucket_index];
      if (!SlotIsFree(bucket_index, old_free_bitmap)) {
        FindOrCreateEntry(std::move(bucket.key.get()),
                          std::move(bucket.value.get()));
      }
    }
  }
};

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_NESTED_HASH_MAP_H_
