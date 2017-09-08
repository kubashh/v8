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

struct ObjectDescriptor {
  int properties_count = 0;
  int elements_count = 0;
  int computed_count = 0;
  int current_computed_index = 0;
  Handle<DescriptorArray> descriptor_array_template;
  Handle<NameDictionary> properties_dictionary_template;
  Handle<SeededNumberDictionary> elements_dictionary_template;
  Handle<FixedArray> computed_properties;

  bool has_computed_properties() const { return computed_count != 0; }

  void CreateTemplates(Isolate* isolate, int slack) {
    Factory* factory = isolate->factory();
    descriptor_array_template = factory->empty_descriptor_array();
    properties_dictionary_template = factory->empty_property_dictionary();
    if (properties_count || has_computed_properties() || slack) {
      if (has_computed_properties()) {
        properties_dictionary_template = NameDictionary::New(
            isolate, properties_count + computed_count + slack);
      } else {
        descriptor_array_template =
            DescriptorArray::Allocate(isolate, 0, properties_count + slack);
      }
    }
    elements_dictionary_template =
        elements_count || computed_count
            ? SeededNumberDictionary::New(isolate,
                                          elements_count + computed_count)
            : factory->empty_slow_element_dictionary();

    computed_properties =
        computed_count
            ? factory->NewFixedArray(computed_count *
                                     ClassBoilerplate::kComputedEntrySize)
            : factory->empty_fixed_array();
  }

  void AddDataConstant(Handle<Name> name, Handle<Object> value,
                       PropertyAttributes attribs) {
    if (has_computed_properties()) {
      PropertyDetails details(kData, attribs, PropertyCellType::kNoCell);
      properties_dictionary_template = NameDictionary::Add(
          properties_dictionary_template, name, value, details);
    } else {
      Descriptor d = Descriptor::DataConstant(name, value, attribs);
      descriptor_array_template->Append(&d);
    }
  }

  void AddAccessorProperty(Handle<Name> name, Handle<Object> accessor,
                           PropertyAttributes attribs) {
    if (has_computed_properties()) {
      PropertyDetails details(kAccessor, attribs, PropertyCellType::kNoCell);
      properties_dictionary_template = NameDictionary::Add(
          properties_dictionary_template, name, accessor, details);
    } else {
      Descriptor d = Descriptor::AccessorConstant(name, accessor, attribs);
      descriptor_array_template->Append(&d);
    }
  }

  void AddComputed(ClassBoilerplate::MethodKind method_kind, int name_index) {
    // Reserve enumeration index for computed property.
    int enum_order = properties_dictionary_template->NextEnumerationIndex();
    properties_dictionary_template->SetNextEnumerationIndex(enum_order + 1);

    int computed_name_entry =
        ClassBoilerplate::ComputedEntryEncodeFull(method_kind, name_index);
    computed_properties->set(
        current_computed_index + ClassBoilerplate::kComputedNameIndex,
        Smi::FromInt(computed_name_entry));
    computed_properties->set(
        current_computed_index +
            ClassBoilerplate::kComputedEnumerationOrderValueIndex,
        Smi::FromInt(enum_order));
    current_computed_index += ClassBoilerplate::kComputedEntrySize;
  }
};

void AddToDescriptorArrayTemplate(
    Isolate* isolate, Handle<DescriptorArray> descriptor_array_template,
    Handle<Name> name, ClassBoilerplate::MethodKind method_kind,
    Handle<Object> value) {
  int entry = descriptor_array_template->Search(
      *name, descriptor_array_template->number_of_descriptors());
  if (entry != DescriptorArray::kNotFound) {
    if (method_kind == ClassBoilerplate::kNormal) {
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
      pair->set(method_kind == ClassBoilerplate::kGetter ? ACCESSOR_GETTER
                                                         : ACCESSOR_SETTER,
                *value);
    }

  } else {
    Descriptor d;
    if (method_kind == ClassBoilerplate::kNormal) {
      d = Descriptor::DataConstant(name, value, DONT_ENUM);
    } else {
      Handle<AccessorPair> pair = isolate->factory()->NewAccessorPair();
      pair->set(method_kind == ClassBoilerplate::kGetter ? ACCESSOR_GETTER
                                                         : ACCESSOR_SETTER,
                *value);
      d = Descriptor::AccessorConstant(name, pair, DONT_ENUM);
    }
    descriptor_array_template->Append(&d);
  }
}

template <typename Dictionary, typename Key>
void ClassBoilerplate::AddComputedEntryToDictionary(
    Isolate* isolate, Handle<Dictionary> dictionary, Key key, int key_index,
    int enum_order, ClassBoilerplate::MethodKind value_kind, Object* value_) {
  int entry = dictionary->FindEntry(isolate, key);

  if (entry != kNotFound) {
    enum_order =
        enum_order < 0 ? 0 : dictionary->DetailsAt(entry).dictionary_index();
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
          dictionary->ValueAtPut(entry, value_);

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
          dictionary->ValueAtPut(entry, value_);
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
          current_pair->set(component, value_);
        }

      } else {
        Handle<AccessorPair> pair(isolate->factory()->NewAccessorPair());
        pair->set(component, value_);
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
        PropertyCellType::kNoCell, enum_order < 0 ? 0 : enum_order);

    if (value_kind == ClassBoilerplate::kNormal) {
      value_handle = handle(value_, isolate);
    } else {
      AccessorComponent component = value_kind == ClassBoilerplate::kGetter
                                        ? ACCESSOR_GETTER
                                        : ACCESSOR_SETTER;
      Handle<AccessorPair> pair(isolate->factory()->NewAccessorPair());
      pair->set(component, value_);
      value_handle = pair;
    }

    Handle<Dictionary> dict =
        i::Dictionary<Dictionary, typename Dictionary::ShapeT>::Add(
            dictionary, key, value_handle, details, &entry);
    CHECK_EQ(*dict, *dictionary);
  }
}

template void
ClassBoilerplate::AddComputedEntryToDictionary<NameDictionary, Handle<Name>>(
    Isolate* isolate, Handle<NameDictionary> dictionary, Handle<Name> key,
    int key_index, int enum_order, ClassBoilerplate::MethodKind value_kind,
    Object* value);

template void ClassBoilerplate::AddComputedEntryToDictionary<
    SeededNumberDictionary, uint32_t>(Isolate* isolate,
                                      Handle<SeededNumberDictionary> dictionary,
                                      uint32_t key, int key_index,
                                      int enum_order,
                                      ClassBoilerplate::MethodKind value_kind,
                                      Object* value);

template <typename Dictionary, typename Key>
void AddToDictionaryTemplate(Isolate* isolate,
                             Handle<Dictionary> dictionary_template, Key key,
                             ClassBoilerplate::MethodKind method_kind,
                             Handle<Object> value) {
  // USE(ClassBoilerplate::AddComputedEntryToDictionary<Dictionary, Key>);
  int entry = dictionary_template->FindEntry(key);

  if (entry != NameDictionary::kNotFound) {
    int enum_order = dictionary_template->DetailsAt(entry).dictionary_index();
    if (method_kind == ClassBoilerplate::kNormal) {
      PropertyDetails details(kData, DONT_ENUM, PropertyCellType::kNoCell,
                              enum_order);
      dictionary_template->DetailsAtPut(entry, details);
      dictionary_template->ValueAtPut(entry, *value);
    } else {
      Object* raw_accessor = dictionary_template->ValueAt(entry);
      AccessorPair* pair;
      if (raw_accessor->IsAccessorPair()) {
        pair = AccessorPair::cast(raw_accessor);
      } else {
        pair = *isolate->factory()->NewAccessorPair();
        PropertyDetails details(kAccessor, DONT_ENUM, PropertyCellType::kNoCell,
                                enum_order);
        dictionary_template->DetailsAtPut(entry, details);
        dictionary_template->ValueAtPut(entry, pair);
      }
      pair->set(method_kind == ClassBoilerplate::kGetter ? ACCESSOR_GETTER
                                                         : ACCESSOR_SETTER,
                *value);
    }

  } else {
    PropertyDetails details(
        method_kind != ClassBoilerplate::kNormal ? kAccessor : kData, DONT_ENUM,
        PropertyCellType::kNoCell);
    if (method_kind != ClassBoilerplate::kNormal) {
      Handle<AccessorPair> pair = isolate->factory()->NewAccessorPair();
      pair->set(method_kind == ClassBoilerplate::kGetter ? ACCESSOR_GETTER
                                                         : ACCESSOR_SETTER,
                *value);
      value = pair;
    }

    Handle<Dictionary> dict =
        Dictionary::Add(dictionary_template, key, value, details, &entry);
    DCHECK_EQ(*dict, *dictionary_template);
    USE(dict);
  }
}

Handle<ClassBoilerplate> ClassBoilerplate::BuildClassBoilerplate(
    Isolate* isolate, ClassLiteral* expr) {
  Handle<Smi> tmp_value(Smi::kZero, isolate);
  ObjectDescriptor static_desc;
  ObjectDescriptor instance_desc;

  for (int i = 0; i < expr->properties()->length(); i++) {
    ClassLiteral::Property* property = expr->properties()->at(i);
    ObjectDescriptor& desc =
        property->is_static() ? static_desc : instance_desc;
    if (property->is_computed_name()) {
      ++desc.computed_count;
    } else {
      if (property->key()->AsLiteral()->IsPropertyName()) {
        ++desc.properties_count;
      } else {
        ++desc.elements_count;
      }
    }
  }
  static_desc.CreateTemplates(isolate, 6);
  instance_desc.CreateTemplates(isolate, 1);

  {
    *tmp_value.location() =
        Smi::FromInt(ClassBoilerplate::kConstructorArgumentIndex);
    instance_desc.AddDataConstant(isolate->factory()->constructor_string(),
                                  tmp_value, DONT_ENUM);
  }

  STATIC_ASSERT(JSFunction::kLengthDescriptorIndex == 0);
  {
    // Add length_accessor.
    PropertyAttributes attribs =
        static_cast<PropertyAttributes>(DONT_ENUM | READ_ONLY);
    Handle<AccessorInfo> length_accessor =
        Accessors::FunctionLengthInfo(isolate, attribs);
    Handle<Name> name(Name::cast(length_accessor->name()), isolate);
    static_desc.AddAccessorProperty(name, length_accessor, attribs);
  }
  {
    // Add prototype_accessor.
    PropertyAttributes attribs =
        static_cast<PropertyAttributes>(DONT_ENUM | DONT_DELETE | READ_ONLY);
    Handle<AccessorInfo> prototype_accessor =
        Accessors::FunctionPrototypeInfo(isolate, attribs);
    Handle<Name> name(Name::cast(prototype_accessor->name()), isolate);
    static_desc.AddAccessorProperty(name, prototype_accessor, attribs);
  }
  if (FunctionLiteral::NeedsHomeObject(expr->constructor())) {
    PropertyAttributes attribs =
        static_cast<PropertyAttributes>(DONT_ENUM | DONT_DELETE | READ_ONLY);
    *tmp_value.location() =
        Smi::FromInt(ClassBoilerplate::kPrototypeArgumentIndex);

    static_desc.AddDataConstant(isolate->factory()->home_object_symbol(),
                                tmp_value, attribs);
  }
  {
    *tmp_value.location() = Smi::FromInt(-expr->start_position());
    static_desc.AddDataConstant(
        isolate->factory()->class_start_position_symbol(), tmp_value,
        DONT_ENUM);

    *tmp_value.location() = Smi::FromInt(-expr->end_position());
    static_desc.AddDataConstant(isolate->factory()->class_end_position_symbol(),
                                tmp_value, DONT_ENUM);
  }

  // Fill in class boilerplate.
  int dynamic_argument_index = ClassBoilerplate::kFirstDynamicArgumentIndex;

  for (int i = 0; i < expr->properties()->length(); i++) {
    ClassLiteral::Property* property = expr->properties()->at(i);

    ClassBoilerplate::MethodKind method_kind;
    switch (property->kind()) {
      case ClassLiteral::Property::METHOD:
        method_kind = ClassBoilerplate::kNormal;
        break;
      case ClassLiteral::Property::GETTER:
        method_kind = ClassBoilerplate::kGetter;
        break;
      case ClassLiteral::Property::SETTER:
        method_kind = ClassBoilerplate::kSetter;
        break;
      case ClassLiteral::Property::FIELD:
        UNREACHABLE();
        break;
    }

    ObjectDescriptor& desc =
        property->is_static() ? static_desc : instance_desc;
    if (property->is_computed_name()) {
      unsigned computed_name_index = dynamic_argument_index++;
      dynamic_argument_index++;  // computed value index
      desc.AddComputed(method_kind, computed_name_index);
      continue;
    }
    int value_index = dynamic_argument_index++;
    *tmp_value.location() = Smi::FromInt(value_index);

    Literal* key_literal = property->key()->AsLiteral();
    Handle<Object> maybe_name = key_literal->value();
    if (!key_literal->IsPropertyName()) {
      uint32_t element;
      if (maybe_name->IsSmi()) {
        element = Smi::ToInt(*maybe_name);
      } else {
        CHECK(Name::cast(*maybe_name)->AsArrayIndex(&element));
      }
      AddToDictionaryTemplate(isolate, desc.elements_dictionary_template,
                              element, method_kind, tmp_value);
      continue;
    }
    DCHECK(maybe_name->IsUniqueName());
    Handle<Name> name = Handle<Name>::cast(maybe_name);

    if (desc.has_computed_properties()) {
      AddToDictionaryTemplate(isolate, desc.properties_dictionary_template,
                              name, method_kind, tmp_value);
    } else {
      AddToDescriptorArrayTemplate(isolate, desc.descriptor_array_template,
                                   name, method_kind, tmp_value);
    }
  }

  bool install_class_name_accessor = false;
  if (!expr->has_name_static_property() &&
      expr->constructor()->has_shared_name()) {
    if (static_desc.has_computed_properties()) {
      install_class_name_accessor = true;
    } else {
      // Set class name accessor if the "name" method was not added yet.
      PropertyAttributes attribs =
          static_cast<PropertyAttributes>(DONT_ENUM | READ_ONLY);
      Handle<AccessorInfo> accessor_info =
          Accessors::FunctionNameInfo(isolate, attribs);
      Handle<Name> name_string = isolate->factory()->name_string();
      DCHECK_EQ(*name_string, accessor_info->name());
      Descriptor d =
          Descriptor::AccessorConstant(name_string, accessor_info, attribs);
      static_desc.descriptor_array_template->Append(&d);
    }
  }

  Handle<ClassBoilerplate> class_boilerplate = Handle<ClassBoilerplate>::cast(
      isolate->factory()->NewFixedArray(kBoileplateLength));

  class_boilerplate->set_install_class_name_accessor(
      install_class_name_accessor);

  class_boilerplate->set_static_properties_template(
      static_desc.has_computed_properties()
          ? static_cast<Object*>(*static_desc.properties_dictionary_template)
          : static_cast<Object*>(*static_desc.descriptor_array_template));
  class_boilerplate->set_static_elements_template(
      *static_desc.elements_dictionary_template);
  class_boilerplate->set_static_computed_properties(
      *static_desc.computed_properties);

  class_boilerplate->set_instance_properties_template(
      instance_desc.has_computed_properties()
          ? static_cast<Object*>(*instance_desc.properties_dictionary_template)
          : static_cast<Object*>(*instance_desc.descriptor_array_template));
  class_boilerplate->set_instance_elements_template(
      *instance_desc.elements_dictionary_template);
  class_boilerplate->set_instance_computed_properties(
      *instance_desc.computed_properties);

  return class_boilerplate;
}

}  // namespace internal
}  // namespace v8
