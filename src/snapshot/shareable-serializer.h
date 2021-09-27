// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SNAPSHOT_SHAREABLE_SERIALIZER_H_
#define V8_SNAPSHOT_SHAREABLE_SERIALIZER_H_

#include "src/snapshot/roots-serializer.h"

namespace v8 {
namespace internal {

class HeapObject;
class ReadOnlySerializer;

class V8_EXPORT_PRIVATE ShareableSerializer : public RootsSerializer {
 public:
  ShareableSerializer(Isolate* isolate, Snapshot::SerializerFlags flags,
                      ReadOnlySerializer* read_only_serializer);
  ~ShareableSerializer() override;
  ShareableSerializer(const ShareableSerializer&) = delete;
  ShareableSerializer& operator=(const ShareableSerializer&) = delete;

  // Terminate the shareable object cache with an undefined value and serialize
  // the string table..
  void FinalizeSerialization();

  // If |obj| can be serialized in the read-only snapshot then add it to the
  // read-only object cache if not already present and emit a
  // ReadOnlyObjectCache bytecode into |sink|. Returns whether this was
  // successful.
  bool SerializeUsingReadOnlyObjectCache(SnapshotByteSink* sink,
                                         Handle<HeapObject> obj);

  // If |obj| can be serialized in the shareable snapshot then add it to the
  // shareable object cache if not already present and emit a
  // ShareableObjectCache bytecode into |sink|. Returns whether this was
  // successful.
  bool SerializeUsingShareableObjectCache(SnapshotByteSink* sink,
                                          Handle<HeapObject> obj);

  static bool IsShareable(Isolate* isolate, HeapObject obj);

 private:
  void SerializeStringTable(StringTable* string_table);

  void SerializeObjectImpl(Handle<HeapObject> obj) override;

  ReadOnlySerializer* read_only_serializer_;

#ifdef DEBUG
  IdentityMap<int, base::DefaultAllocationPolicy> serialized_objects_;
#endif
};

}  // namespace internal
}  // namespace v8

#endif  // V8_SNAPSHOT_SHAREABLE_SERIALIZER_H_
