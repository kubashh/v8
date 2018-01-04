// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/common-utils.h"

#include "src/compiler/node-properties.h"
#include "src/objects/map.h"
#include "src/zone/zone-handle-set.h"

namespace v8 {
namespace internal {
namespace compiler {

MaybeHandle<Map> GetMapWitness(Node* node) {
  ZoneHandleSet<Map> maps;
  Node* receiver = NodeProperties::GetValueInput(node, 1);
  Node* effect = NodeProperties::GetEffectInput(node);
  NodeProperties::InferReceiverMapsResult result =
      NodeProperties::InferReceiverMaps(receiver, effect, &maps);
  if (result == NodeProperties::kReliableReceiverMaps && maps.size() == 1) {
    return maps[0];
  }
  return MaybeHandle<Map>();
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
