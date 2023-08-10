// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unordered_set>

#include "src/builtins/builtins-utils-inl.h"
#include "src/objects/js-struct-inl.h"
#include "src/objects/property-details.h"

namespace v8 {
namespace internal {

constexpr int kMaxJSStructFields = 999;
// Note: For Wasm structs, we currently allow 2000 fields, because there was
// specific demand for that. Ideally we'd have the same limit, but JS structs
// rely on DescriptorArrays and are hence limited to 1020 fields at most.
static_assert(kMaxJSStructFields <= kMaxNumberOfDescriptors);

namespace {

struct NameHandleHasher {
  size_t operator()(Handle<Name> name) const { return name->hash(); }
};

struct UniqueNameHandleEqual {
  bool operator()(Handle<Name> x, Handle<Name> y) const {
    DCHECK(IsUniqueName(*x));
    DCHECK(IsUniqueName(*y));
    return *x == *y;
  }
};

using UniqueNameHandleSet =
    std::unordered_set<Handle<Name>, NameHandleHasher, UniqueNameHandleEqual>;

}  // namespace

BUILTIN(SharedSpaceJSObjectHasInstance) {
  HandleScope scope(isolate);
  Handle<Object> constructor = args.receiver();
  if (!IsJSFunction(*constructor)) {
    return *isolate->factory()->false_value();
  }

  bool result;
  MAYBE_ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, result,
      AlwaysSharedSpaceJSObject::HasInstance(
          isolate, Handle<JSFunction>::cast(constructor),
          args.atOrUndefined(isolate, 1)));
  return *isolate->factory()->ToBoolean(result);
}

namespace {

Maybe<int> GetNumberOfFields(Isolate* isolate,
                             Handle<JSReceiver> property_names) {
  // Treat the list of property names as arraylike.
  Handle<Object> raw_length_number;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, raw_length_number,
      Object::GetLengthFromArrayLike(isolate, property_names), Nothing<int>());
  double num_properties_double = Object::Number(*raw_length_number);
  if (num_properties_double < 0 || num_properties_double > kMaxJSStructFields) {
    THROW_NEW_ERROR_RETURN_VALUE(
        isolate, NewRangeError(MessageTemplate::kStructFieldCountOutOfRange),
        Nothing<int>());
  }
  return Just(static_cast<int>(num_properties_double));
}

Maybe<bool> CollectFieldsAndElements(
    Isolate* isolate, Handle<JSReceiver> property_names, int num_properties,
    std::vector<Handle<Name>>* field_names_in_order,
    UniqueNameHandleSet& field_names_set, std::set<uint32_t>& element_names) {
  Handle<Object> raw_property_name;
  Handle<Name> property_name;
  for (int i = 0; i < num_properties; i++) {
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, raw_property_name,
        JSReceiver::GetElement(isolate, property_names, i), Nothing<bool>());
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(isolate, property_name,
                                     Object::ToName(isolate, raw_property_name),
                                     Nothing<bool>());

    bool is_duplicate;
    size_t index;
    if (!property_name->AsIntegerIndex(&index) ||
        index > JSObject::kMaxElementIndex) {
      property_name = isolate->factory()->InternalizeName(property_name);

      // TODO(v8:12547): Support Symbols?
      if (IsSymbol(*property_name)) {
        THROW_NEW_ERROR_RETURN_VALUE(
            isolate, NewTypeError(MessageTemplate::kSymbolToString),
            Nothing<bool>());
      }

      is_duplicate = !field_names_set.insert(property_name).second;
      // Keep the field names in the original order.
      if (field_names_in_order != nullptr && !is_duplicate) {
        field_names_in_order->push_back(property_name);
      }
    } else {
      is_duplicate = !element_names.insert(static_cast<uint32_t>(index)).second;
    }

    if (is_duplicate) {
      THROW_NEW_ERROR_RETURN_VALUE(
          isolate,
          NewTypeError(MessageTemplate::kDuplicateTemplateProperty,
                       property_name),
          Nothing<bool>());
    }
  }

  return Just(true);
}

}  // namespace

BUILTIN(SharedStructTypeConstructor) {
  DCHECK(v8_flags.shared_string_table);

  HandleScope scope(isolate);
  static const char method_name[] = "SharedStructType";
  auto* factory = isolate->factory();

  Handle<JSReceiver> property_names_arg;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, property_names_arg,
      Object::ToObject(isolate, args.atOrUndefined(isolate, 1), method_name));

  // Treat property_names_arg as arraylike.
  int num_properties;
  MAYBE_ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, num_properties, GetNumberOfFields(isolate, property_names_arg));

  Handle<DescriptorArray> maybe_descriptors;
  Handle<NumberDictionary> elements_template;
  int num_instance_fields = 0;
  int num_agent_local_fields = 0;
  if (num_properties != 0) {
    std::vector<Handle<Name>> field_names_in_order;
    UniqueNameHandleSet field_names_set;
    std::set<uint32_t> element_names;
    MAYBE_RETURN(CollectFieldsAndElements(isolate, property_names_arg,
                                          num_properties, &field_names_in_order,
                                          field_names_set, element_names),
                 ReadOnlyRoots(isolate).exception());

    // Build the set of agent-local field names (if any).
    int num_agent_local_properties = 0;
    UniqueNameHandleSet agent_local_field_names_set;
    Handle<Object> agent_local_property_names_arg =
        args.atOrUndefined(isolate, 2);
    if (!IsUndefined(*agent_local_property_names_arg)) {
      Handle<JSReceiver> agent_local_property_names_object;
      ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
          isolate, agent_local_property_names_object,
          Object::ToObject(isolate, agent_local_property_names_arg,
                           method_name));
      MAYBE_ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
          isolate, num_agent_local_properties,
          GetNumberOfFields(isolate, agent_local_property_names_object));
      std::set<uint32_t> agent_local_element_names;
      MAYBE_RETURN(CollectFieldsAndElements(
                       isolate, agent_local_property_names_object,
                       num_agent_local_properties, nullptr,
                       agent_local_field_names_set, agent_local_element_names),
                   ReadOnlyRoots(isolate).exception());
      if (!agent_local_element_names.empty()) {
        THROW_NEW_ERROR_RETURN_FAILURE(
            isolate,
            NewTypeError(MessageTemplate::kArrayIndicesCannotBeAgentLocal));
      }
    }

    if (!field_names_in_order.empty()) {
      maybe_descriptors = factory->NewDescriptorArray(
          static_cast<int>(field_names_in_order.size()), 0,
          AllocationType::kSharedOld);
      for (const Handle<Name>& field_name : field_names_in_order) {
        int descriptor_number = num_instance_fields + num_agent_local_fields;
        int field_index;
        PropertyLocation location;
        if (agent_local_field_names_set.find(field_name) !=
            agent_local_field_names_set.end()) {
          location = PropertyLocation::kAgentLocal;
          field_index = num_agent_local_fields++;
        } else {
          location = PropertyLocation::kField;
          field_index = num_instance_fields++;
        }
        // Shared struct fields need to be aligned, so make it
        // all tagged.
        PropertyDetails details(PropertyKind::kData, SEALED, location,
                                PropertyConstness::kMutable,
                                Representation::Tagged(), field_index);
        maybe_descriptors->Set(InternalIndex(descriptor_number), *field_name,
                               MaybeObject::FromObject(FieldType::Any()),
                               details);
      }
      maybe_descriptors->Sort();
    }

    if (!element_names.empty()) {
      int nof_elements = static_cast<int>(element_names.size());
      elements_template = NumberDictionary::New(isolate, nof_elements,
                                                AllocationType::kSharedOld);
      for (uint32_t index : element_names) {
        PropertyDetails details(PropertyKind::kData, SEALED,
                                PropertyConstness::kMutable, 0);
        NumberDictionary::UncheckedAdd<Isolate, AllocationType::kSharedOld>(
            isolate, elements_template, index,
            ReadOnlyRoots(isolate).undefined_value_handle(), details);
      }
      elements_template->SetInitialNumberOfElements(nof_elements);
      DCHECK(elements_template->InAnySharedSpace());
    }
  }

  Handle<SharedFunctionInfo> info =
      isolate->factory()->NewSharedFunctionInfoForBuiltin(
          isolate->factory()->empty_string(), Builtin::kSharedStructConstructor,
          FunctionKind::kNormalFunction);
  info->set_internal_formal_parameter_count(JSParameterCount(0));
  info->set_length(0);

  Handle<JSFunction> constructor =
      Factory::JSFunctionBuilder{isolate, info, isolate->native_context()}
          .set_map(isolate->strict_function_with_readonly_prototype_map())
          .Build();

  int instance_size;
  int in_object_properties;
  JSFunction::CalculateInstanceSizeHelper(JS_SHARED_STRUCT_TYPE, false, 0,
                                          num_instance_fields, &instance_size,
                                          &in_object_properties);
  Handle<Map> instance_map =
      factory->NewMap(JS_SHARED_STRUCT_TYPE, instance_size, DICTIONARY_ELEMENTS,
                      in_object_properties, AllocationType::kSharedMap);
  const int num_fields = num_instance_fields + num_agent_local_fields;
  if (num_fields == 0) {
    AlwaysSharedSpaceJSObject::PrepareMapNoEnumerableProperties(*instance_map);
  } else {
    AlwaysSharedSpaceJSObject::PrepareMapWithEnumerableProperties(
        isolate, instance_map, maybe_descriptors, num_fields);
  }

  // Structs have fixed layout ahead of time, so there's no slack.
  int out_of_object_properties = num_instance_fields - in_object_properties;
  if (out_of_object_properties != 0) {
    instance_map->SetOutOfObjectUnusedPropertyFields(0);
  }
  constructor->set_prototype_or_initial_map(*instance_map, kReleaseStore);

  int num_elements = num_properties - num_fields;
  if (num_elements != 0) {
    DCHECK(elements_template->InAnySharedSpace());
    // Abuse the class fields private symbol to store the elements template on
    // shared struct constructors.
    // TODO(v8:12547): Find a better place to store this.
    JSObject::AddProperty(isolate, constructor, factory->class_fields_symbol(),
                          elements_template, NONE);
  }

  JSObject::AddProperty(
      isolate, constructor, factory->has_instance_symbol(),
      handle(isolate->native_context()->shared_space_js_object_has_instance(),
             isolate),
      static_cast<PropertyAttributes>(DONT_ENUM | DONT_DELETE | READ_ONLY));

  return *constructor;
}

BUILTIN(SharedStructConstructor) {
  HandleScope scope(isolate);
  Handle<Object> elements_template;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, elements_template,
      JSReceiver::GetProperty(isolate, args.target(),
                              isolate->factory()->class_fields_symbol()));
  return *isolate->factory()->NewJSSharedStruct(args.target(),
                                                elements_template);
}

BUILTIN(SharedArrayIsSharedArray) {
  HandleScope scope(isolate);
  return isolate->heap()->ToBoolean(
      IsJSSharedArray(*args.atOrUndefined(isolate, 1)));
}

BUILTIN(SharedStructTypeIsSharedStruct) {
  HandleScope scope(isolate);
  return isolate->heap()->ToBoolean(
      IsJSSharedStruct(*args.atOrUndefined(isolate, 1)));
}

BUILTIN(AtomicsMutexIsMutex) {
  HandleScope scope(isolate);
  return isolate->heap()->ToBoolean(
      IsJSAtomicsMutex(*args.atOrUndefined(isolate, 1)));
}

BUILTIN(AtomicsConditionIsCondition) {
  HandleScope scope(isolate);
  return isolate->heap()->ToBoolean(
      IsJSAtomicsCondition(*args.atOrUndefined(isolate, 1)));
}

}  // namespace internal
}  // namespace v8
