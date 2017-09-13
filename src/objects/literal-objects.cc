// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/objects/literal-objects.h"

#include "src/accessors.h"
#include "src/ast/ast.h"
#include "src/factory.h"
#include "src/isolate.h"
#include "src/objects-inl.h"
#include "src/objects/literal-objects-inl.h"

namespace v8 {
namespace internal {

Object* BoilerplateDescription::name(int index) const {
  // get() already checks for out of bounds access, but we do not want to allow
  // access to the last element, if it is the number of properties.
  DCHECK_NE(size(), index);
  return get(2 * index);
}

Object* BoilerplateDescription::value(int index) const {
  return get(2 * index + 1);
}

int BoilerplateDescription::size() const {
  DCHECK_EQ(0, (length() - (this->has_number_of_properties() ? 1 : 0)) % 2);
  // Rounding is intended.
  return length() / 2;
}

int BoilerplateDescription::backing_store_size() const {
  if (has_number_of_properties()) {
    // If present, the last entry contains the number of properties.
    return Smi::ToInt(this->get(length() - 1));
  }
  // If the number is not given explicitly, we assume there are no
  // properties with computed names.
  return size();
}

void BoilerplateDescription::set_backing_store_size(Isolate* isolate,
                                                    int backing_store_size) {
  DCHECK(has_number_of_properties());
  DCHECK_NE(size(), backing_store_size);
  Handle<Object> backing_store_size_obj =
      isolate->factory()->NewNumberFromInt(backing_store_size);
  set(length() - 1, *backing_store_size_obj);
}

bool BoilerplateDescription::has_number_of_properties() const {
  return length() % 2 != 0;
}

namespace {

void AddToDescriptorArrayTemplate(
    Isolate* isolate, Handle<DescriptorArray> descriptor_array_template,
    Handle<Name> name, ClassBoilerplate::ValueKind value_kind,
    Handle<Object> value) {
  int entry = descriptor_array_template->Search(
      *name, descriptor_array_template->number_of_descriptors());
  if (entry != DescriptorArray::kNotFound) {
    if (value_kind == ClassBoilerplate::kNormal) {
      Descriptor d = Descriptor::DataConstant(name, value, DONT_ENUM);
      descriptor_array_template->Set(entry, &d);
    } else {
      Object* raw_accessor = descriptor_array_template->GetValue(entry);
      AccessorPair* pair;
      if (raw_accessor->IsAccessorPair()) {
        pair = AccessorPair::cast(raw_accessor);
      } else {
        pair = *isolate->factory()->NewAccessorPair();
        Descriptor d = Descriptor::DataConstant(name, value, DONT_ENUM);
        descriptor_array_template->Set(entry, &d);
      }
      pair->set(value_kind == ClassBoilerplate::kGetter ? ACCESSOR_GETTER
                                                        : ACCESSOR_SETTER,
                *value);
    }

  } else {
    Descriptor d;
    if (value_kind == ClassBoilerplate::kNormal) {
      d = Descriptor::DataConstant(name, value, DONT_ENUM);
    } else {
      Handle<AccessorPair> pair = isolate->factory()->NewAccessorPair();
      pair->set(value_kind == ClassBoilerplate::kGetter ? ACCESSOR_GETTER
                                                        : ACCESSOR_SETTER,
                *value);
      d = Descriptor::AccessorConstant(name, pair, DONT_ENUM);
    }
    descriptor_array_template->Append(&d);
  }
}

Handle<NameDictionary> DictionaryAddNoUpdateNextEnumerationIndex(
    Handle<NameDictionary> dictionary, Handle<Name> name, Handle<Object> value,
    PropertyDetails details, int* entry_out = nullptr) {
  // Use Dictionary::Add() which does not update next enumeration index.
  return Dictionary<NameDictionary, NameDictionary::ShapeT>::Add(
      dictionary, name, value, details, entry_out);
}

Handle<SeededNumberDictionary> DictionaryAddNoUpdateNextEnumerationIndex(
    Handle<SeededNumberDictionary> dictionary, uint32_t element,
    Handle<Object> value, PropertyDetails details, int* entry_out = nullptr) {
  // SeededNumberDictionary does not maintain the enumeration order, so it's
  // a normal Add().
  return SeededNumberDictionary::Add(dictionary, element, value, details,
                                     entry_out);
}

static const int kNoEnumerationIndex = 0;
STATIC_ASSERT(kNoEnumerationIndex < PropertyDetails::kInitialIndex);

template <typename Dictionary, typename Key>
void AddToDictionaryTemplate(Isolate* isolate, Handle<Dictionary> dictionary,
                             Key key, int key_index,
                             ClassBoilerplate::ValueKind value_kind,
                             Object* value,
                             int enum_order = kNoEnumerationIndex) {
  int entry = dictionary->FindEntry(isolate, key);

  if (entry != kNotFound) {
    if (enum_order == kNoEnumerationIndex) {
      enum_order = dictionary->DetailsAt(entry).dictionary_index();
    }
    Object* existing_value = dictionary->ValueAt(entry);
    if (value_kind == ClassBoilerplate::kNormal) {
      // Computed value is a normal method.
      if (existing_value->IsAccessorPair()) {
        AccessorPair* current_pair = AccessorPair::cast(existing_value);

        Object* getter = current_pair->getter();
        int existing_getter_index = getter->IsSmi() ? Smi::ToInt(getter) : -1;
        Object* setter = current_pair->setter();
        int existing_setter_index = setter->IsSmi() ? Smi::ToInt(setter) : -1;
        if (existing_getter_index < key_index &&
            existing_setter_index < key_index) {
          // Both getter and setter were defined before the computed method,
          // so overwrite both.
          PropertyDetails details(kData, DONT_ENUM, PropertyCellType::kNoCell,
                                  enum_order);
          dictionary->DetailsAtPut(entry, details);
          dictionary->ValueAtPut(entry, value);

        } else {
          if (existing_getter_index < key_index) {
            DCHECK_LT(existing_setter_index, key_index);
            // Getter was defined before the computed method and then it was
            // overwritten by the current computed method which in turn was
            // later overwritten by the setter method. So we clear the getter.
            current_pair->set_getter(*isolate->factory()->null_value());

          } else if (existing_setter_index < key_index) {
            DCHECK_LT(existing_getter_index, key_index);
            // Setter was defined before the computed method and then it was
            // overwritten by the current computed method which in turn was
            // later overwritten by the getter method. So we clear the setter.
            current_pair->set_setter(*isolate->factory()->null_value());
          }
        }
      } else {
        // Overwrite existing value if it was defined before the computed one.
        int existing_value_index = Smi::ToInt(existing_value);
        if (existing_value_index < key_index) {
          PropertyDetails details(kData, DONT_ENUM, PropertyCellType::kNoCell,
                                  enum_order);
          dictionary->DetailsAtPut(entry, details);
          dictionary->ValueAtPut(entry, value);
        }
      }
    } else {
      AccessorComponent component = value_kind == ClassBoilerplate::kGetter
                                        ? ACCESSOR_GETTER
                                        : ACCESSOR_SETTER;
      if (existing_value->IsAccessorPair()) {
        AccessorPair* current_pair = AccessorPair::cast(existing_value);

        Object* component_value = current_pair->get(component);
        int existing_component_index =
            component_value->IsSmi() ? Smi::ToInt(component_value) : -1;
        if (existing_component_index < key_index) {
          current_pair->set(component, value);
        }

      } else {
        Handle<AccessorPair> pair(isolate->factory()->NewAccessorPair());
        pair->set(component, value);
        PropertyDetails details(kAccessor, DONT_ENUM,
                                PropertyCellType::kNoCell);
        dictionary->DetailsAtPut(entry, details);
        dictionary->ValueAtPut(entry, *pair);
      }
    }

  } else {
    // Entry not found
    Handle<Object> value_handle;
    PropertyDetails details(
        value_kind != ClassBoilerplate::kNormal ? kAccessor : kData, DONT_ENUM,
        PropertyCellType::kNoCell, enum_order);

    if (value_kind == ClassBoilerplate::kNormal) {
      value_handle = handle(value, isolate);
    } else {
      AccessorComponent component = value_kind == ClassBoilerplate::kGetter
                                        ? ACCESSOR_GETTER
                                        : ACCESSOR_SETTER;
      Handle<AccessorPair> pair(isolate->factory()->NewAccessorPair());
      pair->set(component, value);
      value_handle = pair;
    }

    // Add value to the dictionary without updating next enumeration index.
    Handle<Dictionary> dict = DictionaryAddNoUpdateNextEnumerationIndex(
        dictionary, key, value_handle, details, &entry);
    // It is crucial to avoid dictionary reallocations because it may remove
    // potential gaps in enumeration indices values that are necessary for
    // inserting computed properties into right places in the enumeration order.
    CHECK_EQ(*dict, *dictionary);
  }
}

}  // namespace

class ObjectDescriptor {
 public:
  void IncComputedCount() { ++computed_count_; }
  void IncPropertiesCount() { ++properties_count_; }
  void IncElementsCount() { ++elements_count_; }

  bool has_computed_properties() const { return computed_count_ != 0; }

  Handle<Object> properties_template() const {
    return has_computed_properties()
               ? Handle<Object>::cast(properties_dictionary_template_)
               : Handle<Object>::cast(descriptor_array_template_);
  }

  Handle<SeededNumberDictionary> elements_template() const {
    return elements_dictionary_template_;
  }

  Handle<FixedArray> computed_properties() const {
    return computed_properties_;
  }

  void CreateTemplates(Isolate* isolate, int slack) {
    Factory* factory = isolate->factory();
    descriptor_array_template_ = factory->empty_descriptor_array();
    properties_dictionary_template_ = factory->empty_property_dictionary();
    if (properties_count_ || has_computed_properties() || slack) {
      if (has_computed_properties()) {
        properties_dictionary_template_ = NameDictionary::New(
            isolate, properties_count_ + computed_count_ + slack);
      } else {
        descriptor_array_template_ =
            DescriptorArray::Allocate(isolate, 0, properties_count_ + slack);
      }
    }
    elements_dictionary_template_ =
        elements_count_ || computed_count_
            ? SeededNumberDictionary::New(isolate,
                                          elements_count_ + computed_count_)
            : factory->empty_slow_element_dictionary();

    computed_properties_ =
        computed_count_
            ? factory->NewFixedArray(computed_count_ *
                                     ClassBoilerplate::kComputedEntrySize)
            : factory->empty_fixed_array();

    smi_handle_ = handle(Smi::kZero, isolate);
  }

  void AddAccessorConstant(Handle<Name> name, Handle<AccessorInfo> accessor,
                           PropertyAttributes attribs) {
    if (has_computed_properties()) {
      DCHECK_EQ(next_property_enumeration_index_,
                properties_dictionary_template_->NextEnumerationIndex());
      PropertyDetails details(kAccessor, attribs, PropertyCellType::kNoCell,
                              next_property_enumeration_index_++);
      properties_dictionary_template_ =
          DictionaryAddNoUpdateNextEnumerationIndex(
              properties_dictionary_template_, name, accessor, details);
      properties_dictionary_template_->SetNextEnumerationIndex(
          next_property_enumeration_index_);
    } else {
      Descriptor d = Descriptor::AccessorConstant(name, accessor, attribs);
      descriptor_array_template_->Append(&d);
    }
  }

  void AddDataConstant(Isolate* isolate, Handle<Name> name, int value_index,
                       PropertyAttributes attribs) {
    AddNamedProperty(isolate, name, ClassBoilerplate::kNormal, value_index);
  }

  void AddNamedProperty(Isolate* isolate, Handle<Name> name,
                        ClassBoilerplate::ValueKind value_kind,
                        int value_index) {
    Smi* value = Smi::FromInt(value_index);
    if (has_computed_properties()) {
      DCHECK_EQ(next_property_enumeration_index_,
                properties_dictionary_template_->NextEnumerationIndex());

      AddToDictionaryTemplate(isolate, properties_dictionary_template_, name,
                              value_index, value_kind, value,
                              next_property_enumeration_index_++);
      properties_dictionary_template_->SetNextEnumerationIndex(
          next_property_enumeration_index_);

    } else {
      *smi_handle_.location() = value;
      AddToDescriptorArrayTemplate(isolate, descriptor_array_template_, name,
                                   value_kind, smi_handle_);
    }
  }

  void AddIndexedProperty(Isolate* isolate, uint32_t element,
                          ClassBoilerplate::ValueKind value_kind,
                          int value_index) {
    Smi* value = Smi::FromInt(value_index);
    AddToDictionaryTemplate(isolate, elements_dictionary_template_, element,
                            value_index, value_kind, value);
  }

  void AddComputed(ClassBoilerplate::ValueKind value_kind, int name_index) {
    // Reserve enumeration index for computed property.
    DCHECK_EQ(next_property_enumeration_index_,
              properties_dictionary_template_->NextEnumerationIndex());
    ++next_property_enumeration_index_;
    int enum_order = properties_dictionary_template_->NextEnumerationIndex();
    properties_dictionary_template_->SetNextEnumerationIndex(enum_order + 1);

    // TODO(ishell): support short encoding.
    int computed_name_entry =
        ClassBoilerplate::ComputedEntryEncodeFull(value_kind, name_index);
    computed_properties_->set(
        current_computed_index_ + ClassBoilerplate::kComputedNameIndex,
        Smi::FromInt(computed_name_entry));
    computed_properties_->set(
        current_computed_index_ +
            ClassBoilerplate::kComputedEnumerationOrderValueIndex,
        Smi::FromInt(enum_order));
    current_computed_index_ += ClassBoilerplate::kComputedEntrySize;
  }

 private:
  int properties_count_ = 0;
  int next_property_enumeration_index_ = PropertyDetails::kInitialIndex;
  int elements_count_ = 0;
  int computed_count_ = 0;
  int current_computed_index_ = 0;

  Handle<DescriptorArray> descriptor_array_template_;
  Handle<NameDictionary> properties_dictionary_template_;
  Handle<SeededNumberDictionary> elements_dictionary_template_;
  Handle<FixedArray> computed_properties_;
  // This temporary handle is used for storing to descriptor array.
  Handle<Smi> smi_handle_;
};

void ClassBoilerplate::AddToPropertiesTemplate(
    Isolate* isolate, Handle<NameDictionary> dictionary, Handle<Name> name,
    int key_index, ClassBoilerplate::ValueKind value_kind, Object* value,
    int enum_order) {
  AddToDictionaryTemplate(isolate, dictionary, name, key_index, value_kind,
                          value, enum_order);
}

void ClassBoilerplate::AddToElementsTemplate(
    Isolate* isolate, Handle<SeededNumberDictionary> dictionary, uint32_t key,
    int key_index, ClassBoilerplate::ValueKind value_kind, Object* value) {
  AddToDictionaryTemplate(isolate, dictionary, key, key_index, value_kind,
                          value);
}

Handle<ClassBoilerplate> ClassBoilerplate::BuildClassBoilerplate(
    Isolate* isolate, ClassLiteral* expr) {
  ObjectDescriptor static_desc;
  ObjectDescriptor instance_desc;

  for (int i = 0; i < expr->properties()->length(); i++) {
    ClassLiteral::Property* property = expr->properties()->at(i);
    ObjectDescriptor& desc =
        property->is_static() ? static_desc : instance_desc;
    if (property->is_computed_name()) {
      desc.IncComputedCount();
    } else {
      if (property->key()->AsLiteral()->IsPropertyName()) {
        desc.IncPropertiesCount();
      } else {
        desc.IncElementsCount();
      }
    }
  }

  //
  // Initialize class object descriptor.
  //
  static_desc.CreateTemplates(isolate, 6);
  STATIC_ASSERT(JSFunction::kLengthDescriptorIndex == 0);
  {
    // Add length_accessor.
    PropertyAttributes attribs =
        static_cast<PropertyAttributes>(DONT_ENUM | READ_ONLY);
    Handle<AccessorInfo> length_accessor =
        Accessors::FunctionLengthInfo(isolate, attribs);
    Handle<Name> name(Name::cast(length_accessor->name()), isolate);
    static_desc.AddAccessorConstant(name, length_accessor, attribs);
  }
  {
    // Add prototype_accessor.
    PropertyAttributes attribs =
        static_cast<PropertyAttributes>(DONT_ENUM | DONT_DELETE | READ_ONLY);
    Handle<AccessorInfo> prototype_accessor =
        Accessors::FunctionPrototypeInfo(isolate, attribs);
    Handle<Name> name(Name::cast(prototype_accessor->name()), isolate);
    static_desc.AddAccessorConstant(name, prototype_accessor, attribs);
  }
  if (FunctionLiteral::NeedsHomeObject(expr->constructor())) {
    PropertyAttributes attribs =
        static_cast<PropertyAttributes>(DONT_ENUM | DONT_DELETE | READ_ONLY);
    static_desc.AddDataConstant(
        isolate, isolate->factory()->home_object_symbol(),
        ClassBoilerplate::kPrototypeArgumentIndex, attribs);
  }
  {
    // Negative values have special handling, they mean the negated value but
    // not an index.
    static_desc.AddDataConstant(
        isolate, isolate->factory()->class_start_position_symbol(),
        -expr->start_position(), DONT_ENUM);

    static_desc.AddDataConstant(isolate,
                                isolate->factory()->class_end_position_symbol(),
                                -expr->end_position(), DONT_ENUM);
  }

  //
  // Initialize prototype object descriptor.
  //
  instance_desc.CreateTemplates(isolate, 1);
  {
    instance_desc.AddDataConstant(
        isolate, isolate->factory()->constructor_string(),
        ClassBoilerplate::kConstructorArgumentIndex, DONT_ENUM);
  }

  //
  // Fill in class boilerplate.
  //
  int dynamic_argument_index = ClassBoilerplate::kFirstDynamicArgumentIndex;

  for (int i = 0; i < expr->properties()->length(); i++) {
    ClassLiteral::Property* property = expr->properties()->at(i);

    ClassBoilerplate::ValueKind value_kind;
    switch (property->kind()) {
      case ClassLiteral::Property::METHOD:
        value_kind = ClassBoilerplate::kNormal;
        break;
      case ClassLiteral::Property::GETTER:
        value_kind = ClassBoilerplate::kGetter;
        break;
      case ClassLiteral::Property::SETTER:
        value_kind = ClassBoilerplate::kSetter;
        break;
      case ClassLiteral::Property::FIELD:
        UNREACHABLE();
        break;
    }

    ObjectDescriptor& desc =
        property->is_static() ? static_desc : instance_desc;
    if (property->is_computed_name()) {
      int computed_name_index = dynamic_argument_index;
      dynamic_argument_index += 2;  // Computed name and value indices.
      desc.AddComputed(value_kind, computed_name_index);
      continue;
    }
    int value_index = dynamic_argument_index++;

    Literal* key_literal = property->key()->AsLiteral();
    Handle<Object> maybe_name = key_literal->value();
    if (key_literal->IsPropertyName()) {
      DCHECK(maybe_name->IsUniqueName());
      Handle<Name> name = Handle<Name>::cast(maybe_name);

      desc.AddNamedProperty(isolate, name, value_kind, value_index);
    } else {
      uint32_t element;
      if (maybe_name->IsSmi()) {
        element = Smi::ToInt(*maybe_name);
      } else {
        CHECK(Name::cast(*maybe_name)->AsArrayIndex(&element));
      }
      desc.AddIndexedProperty(isolate, element, value_kind, value_index);
    }
  }

  // Add name accessor to the class object if necessary.
  bool install_class_name_accessor = false;
  if (!expr->has_name_static_property() &&
      expr->constructor()->has_shared_name()) {
    if (static_desc.has_computed_properties()) {
      install_class_name_accessor = true;
    } else {
      // Set class name accessor if the "name" method was not added yet.
      PropertyAttributes attribs =
          static_cast<PropertyAttributes>(DONT_ENUM | READ_ONLY);
      Handle<AccessorInfo> name_accessor =
          Accessors::FunctionNameInfo(isolate, attribs);
      Handle<Name> name(Name::cast(name_accessor->name()), isolate);
      static_desc.AddAccessorConstant(name, name_accessor, attribs);
    }
  }

  Handle<ClassBoilerplate> class_boilerplate = Handle<ClassBoilerplate>::cast(
      isolate->factory()->NewFixedArray(kBoileplateLength));

  class_boilerplate->set_install_class_name_accessor(
      install_class_name_accessor);

  class_boilerplate->set_static_properties_template(
      *static_desc.properties_template());
  class_boilerplate->set_static_elements_template(
      *static_desc.elements_template());
  class_boilerplate->set_static_computed_properties(
      *static_desc.computed_properties());

  class_boilerplate->set_instance_properties_template(
      *instance_desc.properties_template());
  class_boilerplate->set_instance_elements_template(
      *instance_desc.elements_template());
  class_boilerplate->set_instance_computed_properties(
      *instance_desc.computed_properties());

  return class_boilerplate;
}

}  // namespace internal
}  // namespace v8
