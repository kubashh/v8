// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_STRING_TABLE_H_
#define V8_OBJECTS_STRING_TABLE_H_

#include "src/common/assert-scope.h"
#include "src/objects/string.h"
#include "src/roots/roots.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

class StringTableKey {
 public:
  virtual ~StringTableKey() = default;
  inline StringTableKey(uint32_t hash_field, int length);

  virtual Handle<String> AsHandle(Isolate* isolate) = 0;
  uint32_t hash_field() const {
    DCHECK_NE(0, hash_field_);
    return hash_field_;
  }

  virtual bool IsMatch(String string) = 0;
  inline uint32_t hash() const;
  int length() const { return length_; }

 protected:
  inline void set_hash_field(uint32_t hash_field);

 private:
  uint32_t hash_field_ = 0;
  int length_;
};

class SeqOneByteString;

// StringTable.
//
// No special elements in the prefix and the element size is 1
// because only the string itself (the key) needs to be stored.
class V8_EXPORT_PRIVATE StringTable {
 public:
  static constexpr Smi empty_element() { return Smi::FromInt(0); }
  static constexpr Smi deleted_element() { return Smi::FromInt(1); }

  StringTable();
  ~StringTable();

  int Capacity() const;
  int NumberOfElements() const;

  // Find string in the string table. If it is not there yet, it is
  // added. The return value is the string found.
  Handle<String> LookupString(Isolate* isolate, Handle<String> key);

  // As above, but handles the already internalized strings owned by the
  // deserializer. Since this is for the deserializer, we disallow allocation.
  String LookupStringForDeserializer(Isolate* isolate, String key,
                                     const DisallowHeapAllocation& no_gc);

  template <typename StringTableKey>
  Handle<String> LookupKey(Isolate* isolate, StringTableKey* key);

  // {raw_string} must be a tagged String pointer.
  // Returns a tagged pointer: either a Smi if the string is an array index, an
  // internalized string, or a Smi sentinel.
  static Address TryStringToIndexOrLookupExisting(Isolate* isolate,
                                                  Address raw_string);

  void IterateElements(RootVisitor* visitor);
  void DropOldData();
  void NotifyElementsRemoved(int count);

  void Print();

 private:
  class Data;
  std::unique_ptr<Data> data_;
  base::Mutex write_mutex_;
};

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_STRING_TABLE_H_
