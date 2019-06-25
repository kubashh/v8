// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sstream>

#include "debug-helper-internal.h"
#include "heap-constants.h"
#include "include/v8-internal.h"
#include "src/common/ptr-compr-inl.h"
#include "torque-generated/class-debug-readers-tq.h"

namespace i = v8::internal;

namespace v8_debug_helper_internal {

// Adapts one STRUCT_LIST_GENERATOR entry to (Name, NAME) format.
#define STRUCT_INSTANCE_TYPE_ADAPTER(V, NAME, Name, name) V(Name, NAME)

// INSTANCE_TYPE_CHECKERS_SINGLE_BASE, trimmed down to only classes that have
// layouts defined in .tq files.
// For now, this is a hand-maintained list. Someday Torque may know enough
// about instance types to help with this task.
#define TQ_INSTANCE_TYPES_SINGLE_BASE(V)                       \
  STRUCT_LIST_GENERATOR(STRUCT_INSTANCE_TYPE_ADAPTER, V)       \
  V(ByteArray, BYTE_ARRAY_TYPE)                                \
  V(BytecodeArray, BYTECODE_ARRAY_TYPE)                        \
  V(CallHandlerInfo, CALL_HANDLER_INFO_TYPE)                   \
  V(Cell, CELL_TYPE)                                           \
  V(DescriptorArray, DESCRIPTOR_ARRAY_TYPE)                    \
  V(EmbedderDataArray, EMBEDDER_DATA_ARRAY_TYPE)               \
  V(FeedbackCell, FEEDBACK_CELL_TYPE)                          \
  V(FeedbackVector, FEEDBACK_VECTOR_TYPE)                      \
  V(FixedDoubleArray, FIXED_DOUBLE_ARRAY_TYPE)                 \
  V(Foreign, FOREIGN_TYPE)                                     \
  V(FreeSpace, FREE_SPACE_TYPE)                                \
  V(HeapNumber, HEAP_NUMBER_TYPE)                              \
  V(JSArgumentsObject, JS_ARGUMENTS_TYPE)                      \
  V(JSArray, JS_ARRAY_TYPE)                                    \
  V(JSArrayBuffer, JS_ARRAY_BUFFER_TYPE)                       \
  V(JSArrayIterator, JS_ARRAY_ITERATOR_TYPE)                   \
  V(JSAsyncFromSyncIterator, JS_ASYNC_FROM_SYNC_ITERATOR_TYPE) \
  V(JSAsyncFunctionObject, JS_ASYNC_FUNCTION_OBJECT_TYPE)      \
  V(JSAsyncGeneratorObject, JS_ASYNC_GENERATOR_OBJECT_TYPE)    \
  V(JSBoundFunction, JS_BOUND_FUNCTION_TYPE)                   \
  V(JSDataView, JS_DATA_VIEW_TYPE)                             \
  V(JSDate, JS_DATE_TYPE)                                      \
  V(JSFunction, JS_FUNCTION_TYPE)                              \
  V(JSGlobalObject, JS_GLOBAL_OBJECT_TYPE)                     \
  V(JSGlobalProxy, JS_GLOBAL_PROXY_TYPE)                       \
  V(JSMap, JS_MAP_TYPE)                                        \
  V(JSMessageObject, JS_MESSAGE_OBJECT_TYPE)                   \
  V(JSModuleNamespace, JS_MODULE_NAMESPACE_TYPE)               \
  V(JSPromise, JS_PROMISE_TYPE)                                \
  V(JSProxy, JS_PROXY_TYPE)                                    \
  V(JSRegExp, JS_REGEXP_TYPE)                                  \
  V(JSRegExpStringIterator, JS_REGEXP_STRING_ITERATOR_TYPE)    \
  V(JSSet, JS_SET_TYPE)                                        \
  V(JSStringIterator, JS_STRING_ITERATOR_TYPE)                 \
  V(JSTypedArray, JS_TYPED_ARRAY_TYPE)                         \
  V(JSPrimitiveWrapper, JS_PRIMITIVE_WRAPPER_TYPE)             \
  V(JSFinalizationGroup, JS_FINALIZATION_GROUP_TYPE)           \
  V(JSFinalizationGroupCleanupIterator,                        \
    JS_FINALIZATION_GROUP_CLEANUP_ITERATOR_TYPE)               \
  V(JSWeakMap, JS_WEAK_MAP_TYPE)                               \
  V(JSWeakRef, JS_WEAK_REF_TYPE)                               \
  V(JSWeakSet, JS_WEAK_SET_TYPE)                               \
  V(Map, MAP_TYPE)                                             \
  V(Oddball, ODDBALL_TYPE)                                     \
  V(PreparseData, PREPARSE_DATA_TYPE)                          \
  V(PropertyArray, PROPERTY_ARRAY_TYPE)                        \
  V(PropertyCell, PROPERTY_CELL_TYPE)                          \
  V(SharedFunctionInfo, SHARED_FUNCTION_INFO_TYPE)             \
  V(Symbol, SYMBOL_TYPE)                                       \
  V(WasmExceptionObject, WASM_EXCEPTION_TYPE)                  \
  V(WasmGlobalObject, WASM_GLOBAL_TYPE)                        \
  V(WasmMemoryObject, WASM_MEMORY_TYPE)                        \
  V(WasmModuleObject, WASM_MODULE_TYPE)                        \
  V(WasmTableObject, WASM_TABLE_TYPE)                          \
  V(WeakArrayList, WEAK_ARRAY_LIST_TYPE)                       \
  V(WeakCell, WEAK_CELL_TYPE)
#ifdef V8_INTL_SUPPORT

#define TQ_INSTANCE_TYPES_SINGLE(V)                          \
  TQ_INSTANCE_TYPES_SINGLE_BASE(V)                           \
  V(JSV8BreakIterator, JS_INTL_V8_BREAK_ITERATOR_TYPE)       \
  V(JSCollator, JS_INTL_COLLATOR_TYPE)                       \
  V(JSDateTimeFormat, JS_INTL_DATE_TIME_FORMAT_TYPE)         \
  V(JSListFormat, JS_INTL_LIST_FORMAT_TYPE)                  \
  V(JSLocale, JS_INTL_LOCALE_TYPE)                           \
  V(JSNumberFormat, JS_INTL_NUMBER_FORMAT_TYPE)              \
  V(JSPluralRules, JS_INTL_PLURAL_RULES_TYPE)                \
  V(JSRelativeTimeFormat, JS_INTL_RELATIVE_TIME_FORMAT_TYPE) \
  V(JSSegmentIterator, JS_INTL_SEGMENT_ITERATOR_TYPE)        \
  V(JSSegmenter, JS_INTL_SEGMENTER_TYPE)

#else

#define TQ_INSTANCE_TYPES_SINGLE(V) TQ_INSTANCE_TYPES_SINGLE_BASE(V)

#endif  // V8_INTL_SUPPORT

// Likewise, these are the subset of INSTANCE_TYPE_CHECKERS_RANGE that have
// definitions in .tq files, rearranged with more specific things first.
#define TQ_INSTANCE_TYPES_RANGE(V)                             \
  V(Context, FIRST_CONTEXT_TYPE, LAST_CONTEXT_TYPE)            \
  V(FixedArray, FIRST_FIXED_ARRAY_TYPE, LAST_FIXED_ARRAY_TYPE) \
  V(Microtask, FIRST_MICROTASK_TYPE, LAST_MICROTASK_TYPE)      \
  V(String, FIRST_STRING_TYPE, LAST_STRING_TYPE)               \
  V(Name, FIRST_NAME_TYPE, LAST_NAME_TYPE)                     \
  V(WeakFixedArray, FIRST_WEAK_FIXED_ARRAY_TYPE, LAST_WEAK_FIXED_ARRAY_TYPE)

std::tuple<d::TypeCheckResult, i::InstanceType> GetInstanceTypeFromHint(
    std::string type_hint) {
  // Allow optional fully-qualified name
  const char* prefix = "v8::internal::";
  if (strncmp(type_hint.c_str(), prefix, strlen(prefix)) == 0) {
    type_hint = type_hint.substr(strlen(prefix));
  }

#define TYPE_HINT_CHECK(ClassName, INSTANCE_TYPE)             \
  if (type_hint == #ClassName) {                              \
    return std::make_tuple(d::TypeCheckResult::kUsedTypeHint, \
                           i::INSTANCE_TYPE);                 \
  }
  TQ_INSTANCE_TYPES_SINGLE(TYPE_HINT_CHECK)
#undef TYPE_HINT_CHECK

  return std::make_tuple(d::TypeCheckResult::kUnknownInstanceType,
                         static_cast<i::InstanceType>(-1));
}

std::unique_ptr<ObjectPropertiesResult> GetHeapObjectProperties(
    uintptr_t address, d::MemoryAccessor accessor, i::InstanceType type,
    d::TypeCheckResult type_check_result) {
  std::vector<std::unique_ptr<ObjectProperty>> props;
  std::string type_name;
  std::string brief;

  // Dispatch to the appropriate method for each instance type. After calling
  // the generated method to fetch properties, we can add custom properties.
  switch (type) {
#define INSTANCE_TYPE_CASE(ClassName, INSTANCE_TYPE)        \
  case i::INSTANCE_TYPE:                                    \
    type_name = #ClassName;                                 \
    props = Tq##ClassName(address).GetProperties(accessor); \
    break;
    TQ_INSTANCE_TYPES_SINGLE(INSTANCE_TYPE_CASE)
#undef INSTANCE_TYPE_CASE

    default:

#define INSTANCE_RANGE_CASE(ClassName, FIRST_TYPE, LAST_TYPE) \
  if (type >= i::FIRST_TYPE && type <= i::LAST_TYPE) {        \
    type_name = #ClassName;                                   \
    props = Tq##ClassName(address).GetProperties(accessor);   \
    break;                                                    \
  }
      TQ_INSTANCE_TYPES_RANGE(INSTANCE_RANGE_CASE)
#undef INSTANCE_RANGE_CASE

      type_check_result = d::TypeCheckResult::kUnknownInstanceType;
      type_name = "Object";
      break;
  }

  // If the brief description was not set in the relevant case above, fill
  // it with a generic representation.
  if (brief.empty()) {
    std::stringstream brief_stream;
    brief_stream << "0x" << std::hex << address << " <" << type_name << ">";
    brief = brief_stream.str();
  }

  return std::make_unique<ObjectPropertiesResult>(
      type_check_result, brief, "v8::internal::" + type_name, std::move(props));
}

#undef STRUCT_INSTANCE_TYPE_ADAPTER
#undef TQ_INSTANCE_TYPES_SINGLE_BASE
#undef TQ_INSTANCE_TYPES_SINGLE
#undef TQ_INSTANCE_TYPES_RANGE

std::unique_ptr<ObjectPropertiesResult> GetHeapObjectProperties(
    uintptr_t address, d::MemoryAccessor memory_accessor, const d::Roots& roots,
    const char* type_hint) {
  // Try to figure out the heap range, for pointer compression (this is unused
  // if pointer compression is disabled).
  uintptr_t any_uncompressed_ptr = 0;
  if (!IsPointerCompressed(address)) any_uncompressed_ptr = address;
  if (any_uncompressed_ptr == 0) any_uncompressed_ptr = roots.any_heap_pointer;
  if (any_uncompressed_ptr == 0) any_uncompressed_ptr = roots.map_space;
  if (any_uncompressed_ptr == 0) any_uncompressed_ptr = roots.old_space;
  if (any_uncompressed_ptr == 0) any_uncompressed_ptr = roots.read_only_space;
  if (any_uncompressed_ptr == 0) {
    // We can't figure out the heap range. Just check for known objects.
    std::string brief = FindKnownObject(address, roots);
    if (brief.empty()) brief = "(unknown)";
    return std::make_unique<ObjectPropertiesResult>(
        d::TypeCheckResult::kUnableToDecompress, brief, "v8::internal::Object",
        std::vector<std::unique_ptr<ObjectProperty>>());
  }

  // TODO It seems that the space roots are at predictable offsets within the
  // heap reservation block when pointer compression is enabled, so we should be
  // able to set those here.

  address = Decompress(address, any_uncompressed_ptr);
  // From here on all addresses should be decompressed.

  TqHeapObject heap_object(address);
  Value<uintptr_t> map_ptr = heap_object.GetMapValue(memory_accessor);
  if (map_ptr.validity != d::MemoryAccessResult::kOk) {
    // If we can't read the object itself, maybe we can still find its pointer
    // in the list of known objects.
    std::string brief = FindKnownObject(address, roots);
    if (brief.empty()) brief = "(unknown)";
    return std::make_unique<ObjectPropertiesResult>(
        map_ptr.validity == d::MemoryAccessResult::kAddressNotValid
            ? d::TypeCheckResult::kObjectPointerInvalid
            : d::TypeCheckResult::kObjectPointerValidButInaccessible,
        brief, "v8::internal::Object",
        std::vector<std::unique_ptr<ObjectProperty>>());
  }
  d::TypeCheckResult type_check_result = d::TypeCheckResult::kUsedMap;
  Value<i::InstanceType> instance_type =
      TqMap(map_ptr.value).GetInstanceTypeValue(memory_accessor);
  if (instance_type.validity != d::MemoryAccessResult::kOk) {
    i::InstanceType type_from_hint;
    if (type_hint != nullptr) {
      std::tie(type_check_result, type_from_hint) =
          GetInstanceTypeFromHint(type_hint);
    }
    if (type_check_result == d::TypeCheckResult::kUsedTypeHint) {
      instance_type.validity = d::MemoryAccessResult::kOk;
      instance_type.value = type_from_hint;
    } else {
      // TODO use known maps here. If known map is just a guess (because root
      // pointers weren't provided), then return a synthetic
      // property with the more specific type. Then the caller could presumably
      // ask us again with the type hint we provided. Otherwise, just go ahead
      // and use it to generate properties.
      return std::make_unique<ObjectPropertiesResult>(
          map_ptr.validity == d::MemoryAccessResult::kAddressNotValid
              ? d::TypeCheckResult::kMapPointerInvalid
              : d::TypeCheckResult::kMapPointerValidButInaccessible,
          "(unknown)", "v8::internal::Object",
          std::vector<std::unique_ptr<ObjectProperty>>());
    }
  }
  return GetHeapObjectProperties(address, memory_accessor, instance_type.value,
                                 type_check_result);
}

std::unique_ptr<ObjectPropertiesResult> GetObjectPropertiesImpl(
    uintptr_t address, d::MemoryAccessor memory_accessor, const d::Roots& roots,
    const char* type_hint) {
  std::vector<std::unique_ptr<ObjectProperty>> props;
  if (i::Internals::HasHeapObjectTag(address)) {
    if (static_cast<uint32_t>(address) == i::kClearedWeakHeapObjectLower32) {
      return std::make_unique<ObjectPropertiesResult>(
          d::TypeCheckResult::kWeakRef, "cleared weak ref",
          "v8::internal::HeapObject", std::move(props));
    }
    std::unique_ptr<ObjectPropertiesResult> result =
        GetHeapObjectProperties(address, memory_accessor, roots, type_hint);
    if ((address & i::kHeapObjectTagMask) == i::kWeakHeapObjectTag) {
      result->Prepend("weak ref to ");
    }
    return result;
  }

  // For smi values, construct a response with a description representing the
  // untagged value.
  int32_t value = i::PlatformSmiTagging::SmiToInt(address);
  std::stringstream stream;
  stream << value << " (0x" << std::hex << value << ")";
  return std::make_unique<ObjectPropertiesResult>(
      d::TypeCheckResult::kSmi, stream.str(), "v8::internal::Smi",
      std::move(props));
}

}  // namespace v8_debug_helper_internal

namespace di = v8_debug_helper_internal;

extern "C" {
V8_DEBUG_HELPER_EXPORT d::ObjectPropertiesResult*
_v8_debug_helper_GetObjectProperties(uintptr_t object,
                                     d::MemoryAccessor memory_accessor,
                                     const d::Roots& heap_roots,
                                     const char* type_hint) {
  return di::GetObjectPropertiesImpl(object, memory_accessor, heap_roots,
                                     type_hint)
      .release()
      ->GetPublicView();
}
V8_DEBUG_HELPER_EXPORT void _v8_debug_helper_Free_ObjectPropertiesResult(
    d::ObjectPropertiesResult* result) {
  std::unique_ptr<di::ObjectPropertiesResult> ptr(
      static_cast<di::ObjectPropertiesResultExtended*>(result)->base);
}
}
