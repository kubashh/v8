// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_IC_HANDLER_CONFIGURATION_INL_H_
#define V8_IC_HANDLER_CONFIGURATION_INL_H_

#include "src/ic/handler-configuration.h"

#include "src/handles/handles-inl.h"
#include "src/objects/data-handler-inl.h"
#include "src/objects/field-index-inl.h"
#include "src/objects/objects-inl.h"
#include "src/objects/smi.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

OBJECT_CONSTRUCTORS_IMPL(LoadHandler, DataHandler)

CAST_ACCESSOR(LoadHandler)

// static
ArrayInlineInfo ArrayInlineInfo::DecodeValue(int encoded_value) {
  if (encoded_value == LoadHandler::ArrayInlineInfoBits::kMax)
    return ArrayInlineInfo(false, ElementsKind::NO_ELEMENTS);
  DCHECK_LE(encoded_value, ElementsKind::LAST_FAST_ELEMENTS_KIND);
  return ArrayInlineInfo(true, (ElementsKind)encoded_value);
}

int ArrayInlineInfo::GetEncoding() {
  DCHECK_IMPLIES(supports_fast_array_resize, IsFastElementsKind(elements_kind));
  // This information is used by TurboFan to inline array builtins. TurboFan
  // only supports inlining when the arrays have fast elements kind. So we only
  // encode fast elements kind to limit the number of bits needed to encode.
  // Assert that the fast elements kind fit in specified number of bits.
  // ArrayInlineInfo::kMax value is used to encode that arrays don't support
  // fast array resize.
  // TODO(mythria): Also expand this to include information about array
  // iteration so other array builtins like Array.slice can be inlined too.
  STATIC_ASSERT(ElementsKind::LAST_FAST_ELEMENTS_KIND <
                LoadHandler::ArrayInlineInfoBits::kMax);
  int array_inline_info_encoding = supports_fast_array_resize
                                       ? elements_kind
                                       : LoadHandler::ArrayInlineInfoBits::kMax;
  return array_inline_info_encoding;
}

// Decodes kind from Smi-handler.
LoadHandler::Kind LoadHandler::GetHandlerKind(Smi smi_handler) {
  return KindBits::decode(smi_handler.value());
}

Handle<Smi> LoadHandler::LoadNormal(Isolate* isolate) {
  int config = KindBits::encode(kNormal);
  return handle(Smi::FromInt(config), isolate);
}

Handle<Smi> LoadHandler::LoadGlobal(Isolate* isolate) {
  int config = KindBits::encode(kGlobal);
  return handle(Smi::FromInt(config), isolate);
}

Handle<Smi> LoadHandler::LoadInterceptor(Isolate* isolate) {
  int config = KindBits::encode(kInterceptor);
  return handle(Smi::FromInt(config), isolate);
}

Handle<Smi> LoadHandler::LoadSlow(Isolate* isolate) {
  int config = KindBits::encode(kSlow);
  return handle(Smi::FromInt(config), isolate);
}

Handle<Smi> LoadHandler::LoadField(Isolate* isolate, FieldIndex field_index,
                                   ArrayInlineInfo info) {
  int config = KindBits::encode(kField) |
               IsInobjectBits::encode(field_index.is_inobject()) |
               IsDoubleBits::encode(field_index.is_double()) |
               FieldIndexBits::encode(field_index.index()) |
               ArrayInlineInfoBits::encode(info.GetEncoding());
  return handle(Smi::FromInt(config), isolate);
}

Handle<Smi> LoadHandler::LoadConstantFromPrototype(Isolate* isolate) {
  int config = KindBits::encode(kConstantFromPrototype);
  return handle(Smi::FromInt(config), isolate);
}

Handle<Smi> LoadHandler::LoadAccessor(Isolate* isolate, int descriptor) {
  int config = KindBits::encode(kAccessor) | DescriptorBits::encode(descriptor);
  return handle(Smi::FromInt(config), isolate);
}

Handle<Smi> LoadHandler::LoadProxy(Isolate* isolate) {
  int config = KindBits::encode(kProxy);
  return handle(Smi::FromInt(config), isolate);
}

Handle<Smi> LoadHandler::LoadNativeDataProperty(Isolate* isolate,
                                                int descriptor) {
  int config = KindBits::encode(kNativeDataProperty) |
               DescriptorBits::encode(descriptor);
  return handle(Smi::FromInt(config), isolate);
}

Handle<Smi> LoadHandler::LoadApiGetter(Isolate* isolate,
                                       bool holder_is_receiver) {
  int config = KindBits::encode(
      holder_is_receiver ? kApiGetter : kApiGetterHolderIsPrototype);
  return handle(Smi::FromInt(config), isolate);
}

Handle<Smi> LoadHandler::LoadModuleExport(Isolate* isolate, int index) {
  int config =
      KindBits::encode(kModuleExport) | ExportsIndexBits::encode(index);
  return handle(Smi::FromInt(config), isolate);
}

Handle<Smi> LoadHandler::LoadNonExistent(Isolate* isolate) {
  int config = KindBits::encode(kNonExistent);
  return handle(Smi::FromInt(config), isolate);
}

Handle<Smi> LoadHandler::LoadElement(Isolate* isolate,
                                     ElementsKind elements_kind,
                                     bool convert_hole_to_undefined,
                                     bool is_js_array,
                                     KeyedAccessLoadMode load_mode) {
  int config =
      KindBits::encode(kElement) |
      AllowOutOfBoundsBits::encode(load_mode == LOAD_IGNORE_OUT_OF_BOUNDS) |
      ElementsKindBits::encode(elements_kind) |
      ConvertHoleBits::encode(convert_hole_to_undefined) |
      IsJsArrayBits::encode(is_js_array);
  return handle(Smi::FromInt(config), isolate);
}

Handle<Smi> LoadHandler::LoadIndexedString(Isolate* isolate,
                                           KeyedAccessLoadMode load_mode) {
  int config =
      KindBits::encode(kIndexedString) |
      AllowOutOfBoundsBits::encode(load_mode == LOAD_IGNORE_OUT_OF_BOUNDS);
  return handle(Smi::FromInt(config), isolate);
}

OBJECT_CONSTRUCTORS_IMPL(StoreHandler, DataHandler)

CAST_ACCESSOR(StoreHandler)

Handle<Smi> StoreHandler::StoreGlobalProxy(Isolate* isolate) {
  int config = KindBits::encode(kGlobalProxy);
  return handle(Smi::FromInt(config), isolate);
}

Handle<Smi> StoreHandler::StoreNormal(Isolate* isolate) {
  int config = KindBits::encode(kNormal);
  return handle(Smi::FromInt(config), isolate);
}

Handle<Smi> StoreHandler::StoreInterceptor(Isolate* isolate) {
  int config = KindBits::encode(kInterceptor);
  return handle(Smi::FromInt(config), isolate);
}

Handle<Smi> StoreHandler::StoreSlow(Isolate* isolate,
                                    KeyedAccessStoreMode store_mode) {
  int config =
      KindBits::encode(kSlow) | KeyedAccessStoreModeBits::encode(store_mode);
  return handle(Smi::FromInt(config), isolate);
}

Handle<Smi> StoreHandler::StoreProxy(Isolate* isolate) {
  int config = KindBits::encode(kProxy);
  return handle(Smi::FromInt(config), isolate);
}

Handle<Smi> StoreHandler::StoreField(Isolate* isolate, Kind kind,
                                     int descriptor, FieldIndex field_index,
                                     Representation representation) {
  DCHECK(!representation.IsNone());
  DCHECK(kind == kField || kind == kConstField);

  int config = KindBits::encode(kind) |
               IsInobjectBits::encode(field_index.is_inobject()) |
               RepresentationBits::encode(representation.kind()) |
               DescriptorBits::encode(descriptor) |
               FieldIndexBits::encode(field_index.index());
  return handle(Smi::FromInt(config), isolate);
}

Handle<Smi> StoreHandler::StoreField(Isolate* isolate, int descriptor,
                                     FieldIndex field_index,
                                     PropertyConstness constness,
                                     Representation representation) {
  Kind kind = constness == PropertyConstness::kMutable ? kField : kConstField;
  return StoreField(isolate, kind, descriptor, field_index, representation);
}

Handle<Smi> StoreHandler::StoreNativeDataProperty(Isolate* isolate,
                                                  int descriptor) {
  int config = KindBits::encode(kNativeDataProperty) |
               DescriptorBits::encode(descriptor);
  return handle(Smi::FromInt(config), isolate);
}

Handle<Smi> StoreHandler::StoreAccessor(Isolate* isolate, int descriptor) {
  int config = KindBits::encode(kAccessor) | DescriptorBits::encode(descriptor);
  return handle(Smi::FromInt(config), isolate);
}

Handle<Smi> StoreHandler::StoreApiSetter(Isolate* isolate,
                                         bool holder_is_receiver) {
  int config = KindBits::encode(
      holder_is_receiver ? kApiSetter : kApiSetterHolderIsPrototype);
  return handle(Smi::FromInt(config), isolate);
}

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_IC_HANDLER_CONFIGURATION_INL_H_
