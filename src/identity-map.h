// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_IDENTITY_MAP_H_
#define V8_IDENTITY_MAP_H_

#include "src/base/functional.h"
#include "src/handles.h"

namespace v8 {
namespace internal {

// Forward declarations.
class Heap;

// Base class of identity maps contains shared code for all template
// instantions.
class IdentityMapBase {
 public:
  bool empty() const { return size_ == 0; }
  int size() const { return size_; }
  int capacity() const { return capacity_; }

 protected:
  // Allow Tester to access internals, including changing the address of objects
  // within the {keys_} array in order to simulate a moving GC.
  friend class IdentityMapTester;

  typedef void** RawEntry;

  explicit IdentityMapBase(Heap* heap)
      : heap_(heap),
        gc_counter_(-1),
        size_(0),
        capacity_(0),
        mask_(0),
        keys_(nullptr),
        values_(nullptr) {}
  virtual ~IdentityMapBase();

  RawEntry GetEntry(Object* key);
  RawEntry FindEntry(Object* key);
  void* DeleteEntry(Object* key);
  void* DeleteIndex(int index);
  void Clear();

  RawEntry EntryAtIndex(int index) const;
  int NextIndex(int index) const;

  virtual void** NewPointerArray(size_t length) = 0;
  virtual void DeleteArray(void* array) = 0;

 private:
  // Internal implementation should not be called directly by subclasses.
  int ScanKeysFor(Object* address);
  int InsertKey(Object* address);
  int Lookup(Object* key);
  int LookupOrInsert(Object* key);
  void Rehash();
  void Resize();
  int Hash(Object* address);

  base::hash<uintptr_t> hasher_;
  Heap* heap_;
  int gc_counter_;
  int size_;
  int capacity_;
  int mask_;
  Object** keys_;
  void** values_;
};

// Implements an identity map from object addresses to a given value type {V}.
// The map is robust w.r.t. garbage collection by synchronization with the
// supplied {heap}.
//  * Keys are treated as strong roots.
//  * SMIs are valid keys, except SMI #0.
//  * The value type {V} must be reinterpret_cast'able to {void*}
//  * The value type {V} must not be a heap type.
template <typename V, class AllocationPolicy>
class IdentityMap : public IdentityMapBase {
 public:
  explicit IdentityMap(Heap* heap,
                       AllocationPolicy allocator = AllocationPolicy())
      : IdentityMapBase(heap), allocator_(allocator) {}
  ~IdentityMap() override{};

  // Searches this map for the given key using the object's address
  // as the identity, returning:
  //    found => a pointer to the storage location for the value
  //    not found => a pointer to a new storage location for the value
  V* Get(Handle<Object> key) { return Get(*key); }
  V* Get(Object* key) { return reinterpret_cast<V*>(GetEntry(key)); }

  // Searches this map for the given key using the object's address
  // as the identity, returning:
  //    found => a pointer to the storage location for the value
  //    not found => {nullptr}
  V* Find(Handle<Object> key) { return Find(*key); }
  V* Find(Object* key) { return reinterpret_cast<V*>(FindEntry(key)); }

  // Set the value for the given key.
  void Set(Handle<Object> key, V v) { Set(*key, v); }
  void Set(Object* key, V v) { *(reinterpret_cast<V*>(GetEntry(key))) = v; }

  V Delete(Handle<Object> key) { return Delete(*key); }
  V Delete(Object* key) { return reinterpret_cast<V>(DeleteEntry(key)); }

  // Removes all elements from the map.
  void Clear() { IdentityMapBase::Clear(); }

  class Iterator {
   public:
    Iterator& operator++() {
      index_ = map_->NextIndex(index_);
      return *this;
    }

    Iterator& DeleteAndIncrement() {
      map_->DeleteIndex(index_);
      index_ = map_->NextIndex(index_);
      return *this;
    }

    V* operator*() { return reinterpret_cast<V*>(map_->EntryAtIndex(index_)); }
    V* operator->() { return reinterpret_cast<V*>(map_->EntryAtIndex(index_)); }
    bool operator!=(const Iterator& other) { return index_ != other.index_; }

   private:
    Iterator(IdentityMap* map, int index) : map_(map), index_(index) {}

    IdentityMap* map_;
    int index_;

    friend class IdentityMap;
  };

  Iterator begin() { return Iterator(this, NextIndex(-1)); }
  Iterator end() { return Iterator(this, capacity()); }

 protected:
  void** NewPointerArray(size_t length) override {
    return static_cast<void**>(allocator_.New(sizeof(void*) * length));
  }
  void DeleteArray(void* array) override { allocator_.Delete(array); }

 private:
  AllocationPolicy allocator_;
};
}  // namespace internal
}  // namespace v8

#endif  // V8_IDENTITY_MAP_H_
