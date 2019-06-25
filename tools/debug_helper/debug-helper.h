// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file defines the public interface to v8_debug_helper.

#ifndef V8_TOOLS_DEBUG_HELPER_DEBUG_HELPER_H_
#define V8_TOOLS_DEBUG_HELPER_DEBUG_HELPER_H_

#include <cstdint>
#include <memory>

#if defined(_WIN32)

#ifdef BUILDING_V8_DEBUG_HELPER
#define V8_DEBUG_HELPER_EXPORT __declspec(dllexport)
#elif USING_V8_DEBUG_HELPER
#define V8_DEBUG_HELPER_EXPORT __declspec(dllimport)
#else
#define V8_DEBUG_HELPER_EXPORT
#endif

#else  // defined(_WIN32)

#ifdef BUILDING_V8_DEBUG_HELPER
#define V8_DEBUG_HELPER_EXPORT __attribute__((visibility("default")))
#else
#define V8_DEBUG_HELPER_EXPORT
#endif

#endif  // defined(_WIN32)

namespace v8 {
namespace debug_helper {

// Possible results when attempting to fetch memory from the debuggee.
enum class MemoryAccessResult {
  kOk,
  kAddressNotValid,
  kAddressValidButInaccessible,  // Possible in incomplete dump.
};

enum class SymbolicLookupResult {
  kOk,
  kSymbolNotFound,
};

// Information about how this tool discovered the type of the object.
enum class TypeCheckResult {
  // Success cases (description will be non-null):
  kSmi,
  kWeakRef,
  kUsedMap,
  kUsedTypeHint,

  // Failure cases (description will be null):
  kUnableToDecompress,  // Caller must provide the heap range somehow.
  kObjectPointerInvalid,
  kObjectPointerValidButInaccessible,  // Possible in incomplete dump.
  kMapPointerInvalid,
  kMapPointerValidButInaccessible,  // Possible in incomplete dump.
  kUnknownInstanceType,
};

enum class PropertyKind {
  kSingle,
  kArrayOfKnownSize,
  kArrayOfUnknownSizeDueToInvalidMemory,
  kArrayOfUnknownSizeDueToValidButInaccessibleMemory,
};

struct ObjectProperty {
  const char* name;

  // Statically-determined type, such as from .tq definition.
  const char* type;

  // In some cases, |type| may be a simple type representing a compressed
  // pointer such as v8::internal::TaggedValue. In those cases,
  // |decompressed_type| will contain the type of the object when decompressed.
  // Otherwise, |decompressed_type| will match |type|. In any case, it is safe
  // to pass the |decompressed_type| value as the type_hint on a subsequent call
  // to GetObjectProperties.
  const char* decompressed_type;

  // The address where the property value can be found in the debuggee's address
  // space, or the address of the first value for an array.
  uintptr_t address;

  // If kind indicates an array of unknown size, num_values will be 0 and debug
  // tools should display this property as a raw pointer. Note that there is a
  // semantic difference between num_values=1 and kind=kSingle (normal property)
  // versus num_values=1 and kind=kArrayOfKnownSize (one-element array).
  size_t num_values;

  PropertyKind kind;
};

struct ObjectPropertiesResult {
  TypeCheckResult type_check_result;
  const char* brief;
  const char* type;  // Runtime type of the object.
  size_t num_properties;
  ObjectProperty** properties;
};

// Copies byte_count bytes of memory from the given address in the debuggee to
// the destination buffer.
typedef MemoryAccessResult (*MemoryAccessor)(uintptr_t address,
                                             uint8_t* destination,
                                             size_t byte_count);

// Looks up an item in the debuggee's thread-local storage and writes it to the
// destination.
typedef MemoryAccessResult (*TlsAccessor)(uintptr_t tls_key,
                                          uintptr_t* destination);

// Looks up a global or class-static piece of data in the debuggee by fully-
// qualified name, and writes its address to the destination (does not
// dereference the memory).
typedef SymbolicLookupResult (*GlobalFinder)(const char* name,
                                             uintptr_t* destination);

// Additional data that can help the debugger to be more accurate. Debuggers
// that have access to thread-local storage can call FindRoots to fill this out.
// Any fields you don't know can be set to zero and the debugger will do the
// best it can with the information available.
struct Roots {
  // Beginning of allocated space for various kinds of data. These can help us
  // to detect certain common objects that are placed in memory during startup.
  // These values might be provided via name-value pairs in CrashPad dumps.
  uintptr_t map_space;
  uintptr_t old_space;
  uintptr_t read_only_space;

  // Any valid heap pointer address. On platforms where pointer compression is
  // enabled, this can allow us to get data from compressed pointers even if the
  // other data above is not provided.
  uintptr_t any_heap_pointer;
};

}  // namespace debug_helper
}  // namespace v8

extern "C" {
// Raw library interface. If possible, use functions in v8::debug_helper
// namespace instead because they use smart pointers to prevent leaks.
V8_DEBUG_HELPER_EXPORT v8::debug_helper::ObjectPropertiesResult*
_v8_debug_helper_GetObjectProperties(
    uintptr_t object, v8::debug_helper::MemoryAccessor memory_accessor,
    const v8::debug_helper::Roots& heap_roots, const char* type_hint);
V8_DEBUG_HELPER_EXPORT void _v8_debug_helper_Free_ObjectPropertiesResult(
    v8::debug_helper::ObjectPropertiesResult* result);
V8_DEBUG_HELPER_EXPORT void _v8_debug_helper_FindRoots(
    v8::debug_helper::MemoryAccessor memory_accessor,
    v8::debug_helper::TlsAccessor tls_accessor,
    v8::debug_helper::GlobalFinder global_finder,
    v8::debug_helper::Roots* roots);
}

namespace v8 {
namespace debug_helper {

struct DebugHelperObjectPropertiesResultDeleter {
  void operator()(v8::debug_helper::ObjectPropertiesResult* ptr) {
    _v8_debug_helper_Free_ObjectPropertiesResult(ptr);
  }
};
using ObjectPropertiesResultPtr =
    std::unique_ptr<ObjectPropertiesResult,
                    DebugHelperObjectPropertiesResultDeleter>;

// Get information about the given object pointer (either a tagged pointer
// (compressed or uncompressed), or a SMI). The type hint is only used if the
// object's Map is missing or corrupt. It should be the fully-qualified name of
// a class that inherits from v8::internal::Object.
inline ObjectPropertiesResultPtr GetObjectProperties(
    uintptr_t object, v8::debug_helper::MemoryAccessor memory_accessor,
    const Roots& heap_roots, const char* type_hint = nullptr) {
  return ObjectPropertiesResultPtr(_v8_debug_helper_GetObjectProperties(
      object, memory_accessor, heap_roots, type_hint));
}

// Attempt to find the heap roots by using the Isolate that the current thread's
// local storage points to. Writes the result to the location pointed to by the
// "roots" parameter. Passing this result to future GetObjectProperties calls
// may improve the results.
inline void FindRoots(MemoryAccessor memory_accessor, TlsAccessor tls_accessor,
                      GlobalFinder global_finder, Roots* roots) {
  _v8_debug_helper_FindRoots(memory_accessor, tls_accessor, global_finder,
                             roots);
}

}  // namespace debug_helper
}  // namespace v8

#endif
