// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_LITERAL_OBJECTS_H_
#define V8_OBJECTS_LITERAL_OBJECTS_H_

#include "src/objects.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

class ClassLiteral;

// BoilerplateDescription is a list of properties consisting of name value
// pairs. In addition to the properties, it provides the projected number
// of properties in the backing store. This number includes properties with
// computed names that are not
// in the list.
class BoilerplateDescription : public FixedArray {
 public:
  Object* name(int index) const;
  Object* value(int index) const;

  // The number of boilerplate properties.
  int size() const;

  // Number of boilerplate properties and properties with computed names.
  int backing_store_size() const;

  void set_backing_store_size(Isolate* isolate, int backing_store_size);

  DECL_CAST(BoilerplateDescription)

 private:
  bool has_number_of_properties() const;
};

// Pair of {ElementsKind} and an array of constant values for {ArrayLiteral}
// expressions. Used to communicate with the runtime for literal boilerplate
// creation within the {Runtime_CreateArrayLiteral} method.
class ConstantElementsPair : public Tuple2 {
 public:
  DECL_INT_ACCESSORS(elements_kind)
  DECL_ACCESSORS(constant_values, FixedArrayBase)

  inline bool is_empty() const;

  DECL_CAST(ConstantElementsPair)

  static const int kElementsKindOffset = kValue1Offset;
  static const int kConstantValuesOffset = kValue2Offset;

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(ConstantElementsPair);
};

class ClassBoilerplate : public FixedArray {
 public:
  enum MethodKind { kNormal, kGetter, kSetter };

  struct ComputedEntryShort {
#define COMPUTED_ENCODING_SHORT_BIT_FIELDS(V, _) \
  V(IsShortEncodingBit, bool, 1, _)              \
  V(MethodKindBits, MethodKind, 2, _)            \
  V(NameIndexBits, unsigned, 14, _)              \
  V(EnumerationOrderValueBits, unsigned, 14, _)

    DEFINE_BIT_FIELDS(COMPUTED_ENCODING_SHORT_BIT_FIELDS)
#undef COMPUTED_ENCODING_SHORT_BIT_FIELDS
  };

  struct ComputedEntryFull {
    typedef ComputedEntryShort::IsShortEncodingBit IsShortEncodingBit;
    typedef ComputedEntryShort::MethodKindBits MethodKindBits;
    typedef BitField<unsigned, MethodKindBits::kNext, 29> NameIndexBits;
  };

  enum ArgumentsIndices {
    // kClassBoilerplateArgumentIndex = 0,
    kConstructorArgumentIndex = 1,
    kPrototypeArgumentIndex = 2,
    kFirstDynamicArgumentIndex = 3,
  };

  DECL_CAST(ClassBoilerplate)

  DECL_INT_ACCESSORS(install_class_name_accessor)
  DECL_ACCESSORS(static_properties_template, Object)
  DECL_ACCESSORS(static_elements_template, Object)
  DECL_ACCESSORS(static_computed_properties, FixedArray)
  DECL_ACCESSORS(instance_properties_template, Object)
  DECL_ACCESSORS(instance_elements_template, Object)
  DECL_ACCESSORS(instance_computed_properties, FixedArray)

  template <typename Dictionary, typename Key>
  static void AddComputedEntryToDictionary(
      Isolate* isolate, Handle<Dictionary> dictionary, Key key, int key_index,
      int enum_order, ClassBoilerplate::MethodKind value_kind, Object* value);

  static Handle<ClassBoilerplate> BuildClassBoilerplate(Isolate* isolate,
                                                        ClassLiteral* expr);

  enum {
    kInstallClassNameAccessorIndex,
    kClassPropertiesTemplateIndex,
    kClassElementsTemplateIndex,
    kClassComputedPropertiesIndex,
    kPrototypePropertiesTemplateIndex,
    kPrototypeElementsTemplateIndex,
    kPrototypeComputedPropertiesIndex,
    kBoileplateLength  // last element
  };

  enum ComputedEntryLayoutFull {
    kComputedNameIndex,
    kComputedEnumerationOrderValueIndex,
    kComputedEntrySize,
  };

  static inline bool IsComputedEntryShortEncodable(int max_properties_count);

  static inline int ComputedEntryEncodeShort(MethodKind method_kind,
                                             unsigned name_index,
                                             unsigned enum_order);

  static inline int ComputedEntryEncodeFull(MethodKind method_kind,
                                            unsigned name_index);

  enum {
    kEntryFlagsIndex,
    kEntryNameIndex,
    kClosureArgumentIndexIndex,
    kEntrySize
  };

 private:
  inline int static_method_entry(int index) const;
  inline int instance_method_entry(int index) const;
};

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_LITERAL_OBJECTS_H_
