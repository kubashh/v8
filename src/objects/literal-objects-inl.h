// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_LITERAL_OBJECTS_INL_H_
#define V8_LITERAL_OBJECTS_INL_H_

#include "src/objects-inl.h"
#include "src/objects/literal-objects.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

CAST_ACCESSOR(ClassBoilerplate)

SMI_ACCESSORS(ClassBoilerplate, install_class_name_accessor,
              FixedArray::OffsetOfElementAt(kInstallClassNameAccessorIndex));

ACCESSORS(ClassBoilerplate, static_properties_template, Object,
          FixedArray::OffsetOfElementAt(kClassPropertiesTemplateIndex));

ACCESSORS(ClassBoilerplate, static_elements_template, Object,
          FixedArray::OffsetOfElementAt(kClassElementsTemplateIndex));

ACCESSORS(ClassBoilerplate, static_computed_properties, FixedArray,
          FixedArray::OffsetOfElementAt(kClassComputedPropertiesIndex));

ACCESSORS(ClassBoilerplate, instance_properties_template, Object,
          FixedArray::OffsetOfElementAt(kPrototypePropertiesTemplateIndex));

ACCESSORS(ClassBoilerplate, instance_elements_template, Object,
          FixedArray::OffsetOfElementAt(kPrototypeElementsTemplateIndex));

ACCESSORS(ClassBoilerplate, instance_computed_properties, FixedArray,
          FixedArray::OffsetOfElementAt(kPrototypeComputedPropertiesIndex));

bool ClassBoilerplate::IsComputedEntryShortEncodable(int max_properties_count) {
  STATIC_ASSERT(ComputedEntryFlags::ShortNameIndexBits::kSize ==
                ComputedEntryFlags::ShortEnumerationOrderValueBits::kSize);
  return ComputedEntryFlags::ShortNameIndexBits::is_valid(max_properties_count);
}

int ClassBoilerplate::ComputedEntryEncodeShort(ValueKind value_kind,
                                               unsigned name_index,
                                               unsigned enum_order) {
  DCHECK(ComputedEntryFlags::ShortNameIndexBits::is_valid(name_index));
  DCHECK(
      ComputedEntryFlags::ShortEnumerationOrderValueBits::is_valid(enum_order));
  int flags =
      ComputedEntryFlags::IsShortEncodingBit::encode(true) |
      ComputedEntryFlags::ValueKindBits::encode(value_kind) |
      ComputedEntryFlags::ShortEnumerationOrderValueBits::encode(enum_order) |
      ComputedEntryFlags::ShortNameIndexBits::encode(name_index);
  return flags;
}

int ClassBoilerplate::ComputedEntryEncodeFull(ValueKind value_kind,
                                              unsigned name_index) {
  DCHECK(ComputedEntryFlags::FullNameIndexBits::is_valid(name_index));
  int flags = ComputedEntryFlags::IsShortEncodingBit::encode(false) |
              ComputedEntryFlags::ValueKindBits::encode(value_kind) |
              ComputedEntryFlags::FullNameIndexBits::encode(name_index);
  return flags;
}

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_LITERAL_OBJECTS_INL_H_
