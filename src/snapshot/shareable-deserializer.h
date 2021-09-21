// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SNAPSHOT_SHAREABLE_DESERIALIZER_H_
#define V8_SNAPSHOT_SHAREABLE_DESERIALIZER_H_

#include "src/snapshot/deserializer.h"
#include "src/snapshot/snapshot-data.h"
#include "src/snapshot/snapshot.h"

namespace v8 {
namespace internal {

// Initializes objects in the shared isolate that are not already included in
// the startup snapshot.
class ShareableDeserializer final : public Deserializer<Isolate> {
 public:
  explicit ShareableDeserializer(Isolate* isolate,
                                 const SnapshotData* shareable_data,
                                 bool can_rehash)
      : Deserializer(isolate, shareable_data->Payload(),
                     shareable_data->GetMagicNumber(), false, can_rehash) {}

  // Depending on runtime flags, deserialize shareable objects into the isolate.
  void DeserializeIntoIsolate();

 private:
  void DeserializeStringTable();
};

}  // namespace internal
}  // namespace v8

#endif  // V8_SNAPSHOT_SHAREABLE_DESERIALIZER_H_
