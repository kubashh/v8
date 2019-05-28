// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_MODULE_INL_H_
#define V8_OBJECTS_MODULE_INL_H_

#include "src/objects/js-module.h"
#include "src/objects/module.h"

#include "src/objects/objects-inl.h"  // Needed for write barriers
#include "src/objects/scope-info.h"
#include "src/objects/string-inl.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

OBJECT_CONSTRUCTORS_IMPL(Module, Struct)
OBJECT_CONSTRUCTORS_IMPL(JSModule, Module)
OBJECT_CONSTRUCTORS_IMPL(ModuleInfoEntry, Struct)
OBJECT_CONSTRUCTORS_IMPL(JSModuleNamespace, JSObject)

NEVER_READ_ONLY_SPACE_IMPL(Module)
NEVER_READ_ONLY_SPACE_IMPL(JSModule)

CAST_ACCESSOR(Module)
CAST_ACCESSOR(JSModule)
ACCESSORS(Module, exports, ObjectHashTable, kExportsOffset)
ACCESSORS(Module, module_namespace, HeapObject, kModuleNamespaceOffset)
ACCESSORS(Module, exception, Object, kExceptionOffset)
SMI_ACCESSORS(Module, status, kStatusOffset)
SMI_ACCESSORS(Module, hash, kHashOffset)

bool Module::IsJSModule() const {
  return map().instance_type() == JS_MODULE_TYPE;
}

ACCESSORS(JSModule, code, Object, kCodeOffset)
ACCESSORS(JSModule, regular_exports, FixedArray, kRegularExportsOffset)
ACCESSORS(JSModule, regular_imports, FixedArray, kRegularImportsOffset)
ACCESSORS(JSModule, requested_modules, FixedArray, kRequestedModulesOffset)
ACCESSORS(JSModule, script, Script, kScriptOffset)
ACCESSORS(JSModule, import_meta, Object, kImportMetaOffset)
SMI_ACCESSORS(JSModule, dfs_index, kDfsIndexOffset)
SMI_ACCESSORS(JSModule, dfs_ancestor_index, kDfsAncestorIndexOffset)

ModuleInfo JSModule::info() const {
  return (status() >= kEvaluating)
             ? ModuleInfo::cast(code())
             : GetSharedFunctionInfo().scope_info().ModuleDescriptorInfo();
}

CAST_ACCESSOR(JSModuleNamespace)
ACCESSORS(JSModuleNamespace, module, Module, kModuleOffset)

CAST_ACCESSOR(ModuleInfoEntry)
ACCESSORS(ModuleInfoEntry, export_name, Object, kExportNameOffset)
ACCESSORS(ModuleInfoEntry, local_name, Object, kLocalNameOffset)
ACCESSORS(ModuleInfoEntry, import_name, Object, kImportNameOffset)
SMI_ACCESSORS(ModuleInfoEntry, module_request, kModuleRequestOffset)
SMI_ACCESSORS(ModuleInfoEntry, cell_index, kCellIndexOffset)
SMI_ACCESSORS(ModuleInfoEntry, beg_pos, kBegPosOffset)
SMI_ACCESSORS(ModuleInfoEntry, end_pos, kEndPosOffset)

OBJECT_CONSTRUCTORS_IMPL(ModuleInfo, FixedArray)
CAST_ACCESSOR(ModuleInfo)

FixedArray ModuleInfo::module_requests() const {
  return FixedArray::cast(get(kModuleRequestsIndex));
}

FixedArray ModuleInfo::special_exports() const {
  return FixedArray::cast(get(kSpecialExportsIndex));
}

FixedArray ModuleInfo::regular_exports() const {
  return FixedArray::cast(get(kRegularExportsIndex));
}

FixedArray ModuleInfo::regular_imports() const {
  return FixedArray::cast(get(kRegularImportsIndex));
}

FixedArray ModuleInfo::namespace_imports() const {
  return FixedArray::cast(get(kNamespaceImportsIndex));
}

FixedArray ModuleInfo::module_request_positions() const {
  return FixedArray::cast(get(kModuleRequestPositionsIndex));
}

#ifdef DEBUG
bool ModuleInfo::Equals(ModuleInfo other) const {
  return regular_exports() == other.regular_exports() &&
         regular_imports() == other.regular_imports() &&
         special_exports() == other.special_exports() &&
         namespace_imports() == other.namespace_imports() &&
         module_requests() == other.module_requests() &&
         module_request_positions() == other.module_request_positions();
}
#endif

struct ModuleHandleHash {
  V8_INLINE size_t operator()(Handle<Module> module) const {
    return module->hash();
  }
};

struct ModuleHandleEqual {
  V8_INLINE bool operator()(Handle<Module> lhs, Handle<Module> rhs) const {
    return *lhs == *rhs;
  }
};

struct StringHandleHash {
  V8_INLINE size_t operator()(Handle<String> string) const {
    return string->Hash();
  }
};

struct StringHandleEqual {
  V8_INLINE bool operator()(Handle<String> lhs, Handle<String> rhs) const {
    return lhs->Equals(*rhs);
  }
};

class UnorderedStringSet
    : public std::unordered_set<Handle<String>, StringHandleHash,
                                StringHandleEqual,
                                ZoneAllocator<Handle<String>>> {
 public:
  explicit UnorderedStringSet(Zone* zone)
      : std::unordered_set<Handle<String>, StringHandleHash, StringHandleEqual,
                           ZoneAllocator<Handle<String>>>(
            2 /* bucket count */, StringHandleHash(), StringHandleEqual(),
            ZoneAllocator<Handle<String>>(zone)) {}
};

class UnorderedModuleSet
    : public std::unordered_set<Handle<Module>, ModuleHandleHash,
                                ModuleHandleEqual,
                                ZoneAllocator<Handle<Module>>> {
 public:
  explicit UnorderedModuleSet(Zone* zone)
      : std::unordered_set<Handle<Module>, ModuleHandleHash, ModuleHandleEqual,
                           ZoneAllocator<Handle<Module>>>(
            2 /* bucket count */, ModuleHandleHash(), ModuleHandleEqual(),
            ZoneAllocator<Handle<Module>>(zone)) {}
};

class UnorderedStringMap
    : public std::unordered_map<
          Handle<String>, Handle<Object>, StringHandleHash, StringHandleEqual,
          ZoneAllocator<std::pair<const Handle<String>, Handle<Object>>>> {
 public:
  explicit UnorderedStringMap(Zone* zone)
      : std::unordered_map<
            Handle<String>, Handle<Object>, StringHandleHash, StringHandleEqual,
            ZoneAllocator<std::pair<const Handle<String>, Handle<Object>>>>(
            2 /* bucket count */, StringHandleHash(), StringHandleEqual(),
            ZoneAllocator<std::pair<const Handle<String>, Handle<Object>>>(
                zone)) {}
};

class Module::ResolveSet
    : public std::unordered_map<
          Handle<Module>, UnorderedStringSet*, ModuleHandleHash,
          ModuleHandleEqual,
          ZoneAllocator<std::pair<const Handle<Module>, UnorderedStringSet*>>> {
 public:
  explicit ResolveSet(Zone* zone)
      : std::unordered_map<Handle<Module>, UnorderedStringSet*,
                           ModuleHandleHash, ModuleHandleEqual,
                           ZoneAllocator<std::pair<const Handle<Module>,
                                                   UnorderedStringSet*>>>(
            2 /* bucket count */, ModuleHandleHash(), ModuleHandleEqual(),
            ZoneAllocator<std::pair<const Handle<Module>, UnorderedStringSet*>>(
                zone)),
        zone_(zone) {}

  Zone* zone() const { return zone_; }

 private:
  Zone* zone_;
};

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_MODULE_INL_H_
