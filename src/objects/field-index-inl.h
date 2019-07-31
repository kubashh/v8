// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_FIELD_INDEX_INL_H_
#define V8_OBJECTS_FIELD_INDEX_INL_H_

#include "src/objects/descriptor-array-inl.h"
#include "src/objects/field-index.h"
#include "src/objects/objects-inl.h"

namespace v8 {
namespace internal {

FieldIndex FieldIndex::ForInObjectOffset(int offset, Encoding encoding) {
  DCHECK_IMPLIES(encoding == kWord32, IsAligned(offset, kInt32Size));
  DCHECK_IMPLIES(encoding == kTagged, IsAligned(offset, kTaggedSize));
  DCHECK_IMPLIES(encoding == kDouble, IsAligned(offset, kDoubleSize));
  return FieldIndex(true, offset, encoding, 0, 0);
}

FieldIndex FieldIndex::ForFieldSlot(const Map map, int field_slot_index) {
  return ForFieldSlot(map, field_slot_index,
                      map.IsUnboxedDoubleField(field_slot_index)
                          ? Encoding::kDouble
                          : Encoding::kTagged);
}

FieldIndex FieldIndex::ForFieldSlot(const Map map, int field_slot_index,
                                    Representation representation) {
  return ForFieldSlot(map, field_slot_index, FieldEncoding(representation));
}

FieldIndex FieldIndex::ForFieldSlot(const Map map, int field_slot_index,
                                    Encoding encoding) {
  DCHECK(map.instance_type() >= FIRST_NONSTRING_TYPE);
  int num_inobject_slots = map.TotalInObjectFieldSlots();
  bool is_inobject = field_slot_index < num_inobject_slots;
  int first_inobject_offset;
  int offset;
  if (is_inobject) {
    first_inobject_offset = map.GetInObjectFieldSlotOffset(0);
    offset = map.GetInObjectFieldSlotOffset(field_slot_index);
  } else {
    first_inobject_offset = FixedArray::kHeaderSize;
    field_slot_index -= num_inobject_slots;
    offset = PropertyArray::OffsetOfElementAt(field_slot_index);
  }
  FieldIndex ret = FieldIndex(is_inobject, offset, encoding, num_inobject_slots,
                              first_inobject_offset);
  DCHECK_IMPLIES(map.IsUnboxedDoubleField(ret), encoding == kDouble);
  DCHECK_IMPLIES(FLAG_unbox_double_fields && encoding == kDouble,
                 map.IsUnboxedDoubleField(ret) || !is_inobject);
  return ret;
}

// Returns the index format accepted by the LoadFieldByIndex reduction.
// (In-object: zero-based from (object start + JSObject::kHeaderSize),
// out-of-object: zero-based from FixedArray::kHeaderSize.)
int FieldIndex::GetLoadByFieldIndex() const {
  // For efficiency, the LoadByFieldIndex instruction takes an index that is
  // optimized for quick access. If the property is inline, the index is
  // positive. If it's out-of-line, the encoded index is -raw_index - 1 to
  // disambiguate the zero out-of-line index from the zero inobject case.
  // The index itself is shifted up by one bit, the lower-most bit
  // signifying if the field is a mutable double box (1) or not (0).
  int result = slot_index();
  if (!is_inobject()) {
    result = -result - 1;
  }
  result = static_cast<uint32_t>(result) << 1;
  return is_double() ? (result | 1) : result;
}

FieldIndex FieldIndex::ForDescriptor(Map map, int descriptor_index) {
  PropertyDetails details =
      map.instance_descriptors().GetDetails(descriptor_index);
  return ForDetails(map, details);
}

FieldIndex FieldIndex::ForDescriptor(Isolate* isolate, Map map,
                                     int descriptor_index) {
  PropertyDetails details =
      map.instance_descriptors(isolate).GetDetails(descriptor_index);
  return ForDetails(map, details);
}

FieldIndex FieldIndex::ForDetails(const Map map, PropertyDetails details) {
  int field_slot_index = details.field_slot_index();
  return ForFieldSlot(map, field_slot_index, details.representation());
}

}  // namespace internal
}  // namespace v8

#endif  // V8_OBJECTS_FIELD_INDEX_INL_H_
