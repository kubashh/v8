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
namespace di = v8::debug_helper_internal;

namespace v8 {
namespace debug_helper_internal {

std::tuple<d::TypeCheckResult, i::InstanceType> GetInstanceTypeFromHint(
    std::string type_hint) {
  // Allow optional fully-qualified name
  const char* prefix = "v8::internal::";
  if (strncmp(type_hint.c_str(), prefix, strlen(prefix)) == 0) {
    type_hint = type_hint.substr(strlen(prefix));
  }

  // For now, this is a hand-maintained list. Someday Torque may know enough
  // about instance types to help with this task.
  if (type_hint == "JsArray") {
    return std::make_tuple(d::TypeCheckResult::kUsedTypeHint, i::JS_ARRAY_TYPE);
  }
  // etc..

  return std::make_tuple(d::TypeCheckResult::kUnknownInstanceType,
                         static_cast<i::InstanceType>(-1));
}

std::unique_ptr<ObjectPropertiesResult> GetHeapObjectProperties(
    uintptr_t address, d::MemoryAccessor accessor, i::InstanceType type,
    d::TypeCheckResult type_check_result) {
  std::vector<std::unique_ptr<ObjectProperty>> props;
  std::string type_name;
  std::string brief;

  // Eventually it would be nice to generate some or all of this logic, but for
  // now we must manually dispatch to the appropriate method for each instance
  // type. After calling the generated method to fetch properties, each case
  // can add custom properties if it needs to.
  switch (type) {
    case i::JS_ARRAY_TYPE:
      type_name = "JsArray";
      props = TqJSArray(address).GetProperties(accessor);
      break;
    default:
      type_check_result = d::TypeCheckResult::kUnknownInstanceType;
      type_name = "Object";
      break;
  }

  // If the brief description was not set in the relevant case above, fill
  // it with a generic representation.
  if (brief.empty()) {
    std::stringstream brief_stream;
    brief_stream << std::hex << address << " <" << type_name << ">";
    brief = brief_stream.str();
  }

  return std::make_unique<ObjectPropertiesResult>(
      type_check_result, brief, "v8::internal::" + type_name, std::move(props));
}

bool IsPointerCompressed(uintptr_t address) {
  if (!COMPRESS_POINTERS_BOOL) return false;
  STATIC_ASSERT(i::kPtrComprHeapReservationSize == uintptr_t{1} << 32);
  intptr_t upper_half = static_cast<intptr_t>(address) >> 32;
  // Allow compressed pointers to be either zero-extended or sign-extended by
  // the caller.
  return upper_half == 0 || upper_half == -1;
}

uintptr_t Decompress(uintptr_t address, uintptr_t any_uncompressed_ptr) {
  if (!COMPRESS_POINTERS_BOOL || !IsPointerCompressed(address)) return address;
  return i::DecompressTaggedAny(any_uncompressed_ptr,
                                static_cast<i::Tagged_t>(address));
}

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

}  // namespace debug_helper_internal
}  // namespace v8

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
