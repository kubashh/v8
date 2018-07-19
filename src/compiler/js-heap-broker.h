// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_JS_HEAP_BROKER_H_
#define V8_COMPILER_JS_HEAP_BROKER_H_

#include "src/base/compiler-specific.h"
#include "src/base/optional.h"
#include "src/globals.h"
#include "src/objects.h"
#include "src/zone/zone-containers.h"

namespace v8 {
namespace internal {
namespace compiler {

class DisallowHeapAccess {
  DisallowHeapAllocation no_heap_allocation_;
  DisallowHandleAllocation no_handle_allocation_;
  DisallowHandleDereference no_handle_dereference_;
  DisallowCodeDependencyChange no_dependency_change_;
};

enum class OddballType : uint8_t {
  kNone,     // Not an Oddball.
  kBoolean,  // True or False.
  kUndefined,
  kNull,
  kHole,
  kUninitialized,
  kOther  // Oddball, but none of the above.
};

class HeapObjectType {
 public:
  enum Flag : uint8_t { kUndetectable = 1 << 0, kCallable = 1 << 1 };

  typedef base::Flags<Flag> Flags;

  HeapObjectType(InstanceType instance_type, Flags flags,
                 OddballType oddball_type)
      : instance_type_(instance_type),
        oddball_type_(oddball_type),
        flags_(flags) {
    DCHECK_EQ(instance_type == ODDBALL_TYPE,
              oddball_type != OddballType::kNone);
  }

  OddballType oddball_type() const { return oddball_type_; }
  InstanceType instance_type() const { return instance_type_; }
  Flags flags() const { return flags_; }

  bool is_callable() const { return flags_ & kCallable; }
  bool is_undetectable() const { return flags_ & kUndetectable; }

 private:
  InstanceType const instance_type_;
  OddballType const oddball_type_;
  Flags const flags_;
};

#define HEAP_BROKER_NORMAL_OBJECT_LIST(V) \
  V(AllocationSite)                       \
  V(Cell)                                 \
  V(Code)                                 \
  V(Context)                              \
  V(FeedbackVector)                       \
  V(FixedArray)                           \
  V(FixedArrayBase)                       \
  V(FixedDoubleArray)                     \
  V(HeapNumber)                           \
  V(HeapObject)                           \
  V(InternalizedString)                   \
  V(JSArray)                              \
  V(JSFunction)                           \
  V(JSGlobalProxy)                        \
  V(JSObject)                             \
  V(JSRegExp)                             \
  V(Map)                                  \
  V(Module)                               \
  V(MutableHeapNumber)                    \
  V(Name)                                 \
  V(NativeContext)                        \
  V(PropertyCell)                         \
  V(ScopeInfo)                            \
  V(ScriptContextTable)                   \
  V(SharedFunctionInfo)                   \
  V(String)

#define HEAP_BROKER_OBJECT_LIST(V)  \
  HEAP_BROKER_NORMAL_OBJECT_LIST(V) \
  V(FieldType)

class CompilationDependencies;
class JSHeapBroker;
#define FORWARD_DECL(Name) class Name##Ref;
HEAP_BROKER_OBJECT_LIST(FORWARD_DECL)
#undef FORWARD_DECL

class ObjectRef {
 public:
  template <typename T>
  Handle<T> object() const {
    AllowHandleDereference handle_dereference;
    return Handle<T>::cast(object_);
  }

  OddballType oddball_type() const;

  bool IsSmi() const;
  int AsSmi() const;

  bool equals(const ObjectRef& other) const;

#define HEAP_IS_METHOD_DECL(Name) bool Is##Name() const;
  HEAP_BROKER_NORMAL_OBJECT_LIST(HEAP_IS_METHOD_DECL)
#undef HEAP_IS_METHOD_DECL

#define HEAP_AS_METHOD_DECL(Name) Name##Ref As##Name() const;
  HEAP_BROKER_OBJECT_LIST(HEAP_AS_METHOD_DECL)
#undef HEAP_AS_METHOD_DECL

  StringRef TypeOf() const;
  bool BooleanValue();
  double OddballToNumber() const;

 protected:
  friend class JSHeapBroker;
  ObjectRef(const JSHeapBroker* broker, Handle<Object> object)
      : broker_(broker), object_(object) {}
  const JSHeapBroker* broker() const { return broker_; }

 private:
  const JSHeapBroker* broker_;
  Handle<Object> object_;
};

class FieldTypeRef : public ObjectRef {
 private:
  friend class ObjectRef;
  using ObjectRef::ObjectRef;
};

class HeapObjectRef : public ObjectRef {
 public:
  HeapObjectType type() const;
  MapRef map() const;
  base::Optional<MapRef> TryGetObjectCreateMap() const;
  bool IsSeqString() const;
  bool IsExternalString() const;

 private:
  friend class ObjectRef;
  using ObjectRef::ObjectRef;
};

class PropertyCellRef : public HeapObjectRef {
 public:
  ObjectRef value() const;
  PropertyDetails property_details() const;

 private:
  friend class ObjectRef;
  using HeapObjectRef::HeapObjectRef;
};

class JSObjectRef : public HeapObjectRef {
 public:
  bool IsUnboxedDoubleField(FieldIndex index) const;
  double RawFastDoublePropertyAt(FieldIndex index) const;
  ObjectRef RawFastPropertyAt(FieldIndex index) const;

  FixedArrayBaseRef elements() const;
  void EnsureElementsTenured();
  ElementsKind GetElementsKind();

 private:
  friend class ObjectRef;
  using HeapObjectRef::HeapObjectRef;
};

struct SlackTrackingResult {
  SlackTrackingResult(int instance_sizex, int inobject_property_countx)
      : instance_size(instance_sizex),
        inobject_property_count(inobject_property_countx) {}
  int instance_size;
  int inobject_property_count;
};

class JSFunctionRef : public JSObjectRef {
 public:
  bool HasBuiltinFunctionId() const;
  BuiltinFunctionId GetBuiltinFunctionId() const;
  bool IsConstructor() const;
  bool has_initial_map() const;
  MapRef initial_map() const;

  JSGlobalProxyRef global_proxy() const;
  SlackTrackingResult FinishSlackTracking() const;
  SharedFunctionInfoRef shared() const;
  void EnsureHasInitialMap() const;

 private:
  friend class ObjectRef;
  using JSObjectRef::JSObjectRef;
};

class JSRegExpRef : public JSObjectRef {
 public:
  ObjectRef raw_properties_or_hash() const;
  ObjectRef data() const;
  ObjectRef source() const;
  ObjectRef flags() const;
  ObjectRef last_index() const;

 private:
  friend class ObjectRef;
  using JSObjectRef::JSObjectRef;
};

class HeapNumberRef : public HeapObjectRef {
 public:
  double value() const;

 private:
  friend class ObjectRef;
  using HeapObjectRef::HeapObjectRef;
};

class MutableHeapNumberRef : public HeapObjectRef {
 public:
  double value() const;

 private:
  friend class ObjectRef;
  using HeapObjectRef::HeapObjectRef;
};

class ContextRef : public HeapObjectRef {
 public:
  base::Optional<ContextRef> previous() const;
  ObjectRef get(int index) const;

 private:
  friend class ObjectRef;
  using HeapObjectRef::HeapObjectRef;
};

class NativeContextRef : public ContextRef {
 public:
  ScriptContextTableRef script_context_table() const;

  MapRef fast_aliased_arguments_map() const;
  MapRef sloppy_arguments_map() const;
  MapRef strict_arguments_map() const;
  MapRef js_array_fast_elements_map_index() const;
  MapRef initial_array_iterator_map() const;
  MapRef set_value_iterator_map() const;
  MapRef set_key_value_iterator_map() const;
  MapRef map_key_iterator_map() const;
  MapRef map_value_iterator_map() const;
  MapRef map_key_value_iterator_map() const;
  MapRef iterator_result_map() const;
  MapRef string_iterator_map() const;
  MapRef promise_function_initial_map() const;
  JSFunctionRef array_function() const;

  MapRef GetFunctionMapFromIndex(int index) const;
  MapRef ObjectLiteralMapFromCache() const;
  MapRef GetInitialJSArrayMap(ElementsKind kind) const;

 private:
  friend class ObjectRef;
  using ContextRef::ContextRef;
};

class NameRef : public HeapObjectRef {
 private:
  friend class ObjectRef;
  using HeapObjectRef::HeapObjectRef;
};

class ScriptContextTableRef : public HeapObjectRef {
 public:
  struct LookupResult {
    ContextRef context;
    bool immutable;
    int index;
  };

  base::Optional<LookupResult> lookup(const NameRef& name) const;

 private:
  friend class ObjectRef;
  using HeapObjectRef::HeapObjectRef;
};

class FeedbackVectorRef : public HeapObjectRef {
 public:
  ObjectRef get(FeedbackSlot slot) const;

 private:
  friend class ObjectRef;
  using HeapObjectRef::HeapObjectRef;
};

class AllocationSiteRef : public HeapObjectRef {
 public:
  JSObjectRef boilerplate() const;
  PretenureFlag GetPretenureMode() const;
  bool IsFastLiteral() const;
  ObjectRef nested_site() const;
  bool PointsToLiteral() const;
  ElementsKind GetElementsKind() const;
  bool CanInlineCall() const;

 private:
  friend class ObjectRef;
  using HeapObjectRef::HeapObjectRef;
};

class MapRef : public HeapObjectRef {
 public:
  int instance_size() const;
  InstanceType instance_type() const;
  int GetInObjectProperties() const;
  int NumberOfOwnDescriptors() const;
  PropertyDetails GetPropertyDetails(int i) const;
  NameRef GetPropertyKey(int i) const;
  FieldIndex GetFieldIndexFor(int i) const;
  int GetInObjectPropertyOffset(int index) const;
  ObjectRef constructor_or_backpointer() const;
  ElementsKind elements_kind() const;

  MapRef AsElementsKind(ElementsKind kind) const;

  bool is_stable() const;
  bool has_prototype_slot() const;
  bool is_deprecated() const;
  bool CanBeDeprecated() const;
  bool CanTransition() const;
  bool IsInobjectSlackTrackingInProgress() const;
  MapRef FindFieldOwner(int descriptor) const;
  bool is_dictionary_map() const;
  bool IsJSArrayMap() const;
  bool IsFixedCowArrayMap() const;

  // Concerning the underlying instance_descriptors:
  FieldTypeRef GetFieldType(int descriptor) const;

 private:
  friend class ObjectRef;
  using HeapObjectRef::HeapObjectRef;
};

class FixedArrayBaseRef : public HeapObjectRef {
 public:
  int length() const;

 private:
  friend class ObjectRef;
  using HeapObjectRef::HeapObjectRef;
};

class FixedArrayRef : public FixedArrayBaseRef {
 public:
  ObjectRef get(int i) const;
  bool is_the_hole(int i) const;

 private:
  friend class ObjectRef;
  using FixedArrayBaseRef::FixedArrayBaseRef;
};

class FixedDoubleArrayRef : public FixedArrayBaseRef {
 public:
  double get_scalar(int i) const;
  bool is_the_hole(int i) const;

 private:
  friend class ObjectRef;
  using FixedArrayBaseRef::FixedArrayBaseRef;
};

class JSArrayRef : public JSObjectRef {
 public:
  ElementsKind GetElementsKind() const;
  ObjectRef length() const;

 private:
  friend class ObjectRef;
  using JSObjectRef::JSObjectRef;
};

class ScopeInfoRef : public HeapObjectRef {
 public:
  int ContextLength() const;

 private:
  friend class ObjectRef;
  using HeapObjectRef::HeapObjectRef;
};

class SharedFunctionInfoRef : public HeapObjectRef {
 public:
  int internal_formal_parameter_count() const;
  bool has_duplicate_parameters() const;
  int function_map_index() const;
  FunctionKind kind() const;
  LanguageMode language_mode();
  bool native() const;
  bool HasBreakInfo() const;
  bool HasBuiltinId() const;
  int builtin_id() const;
  bool construct_as_builtin() const;
  bool HasBytecodeArray() const;
  int GetBytecodeArrayRegisterCount() const;

 private:
  friend class ObjectRef;
  using HeapObjectRef::HeapObjectRef;
};

class StringRef : public NameRef {
 public:
  int length() const;
  uint16_t GetFirstChar();
  double ToNumber();

 private:
  friend class ObjectRef;
  using NameRef::NameRef;
};

class ModuleRef : public HeapObjectRef {
 public:
  CellRef GetCell(int cell_index);

 private:
  friend class ObjectRef;
  using HeapObjectRef::HeapObjectRef;
};

class CellRef : public HeapObjectRef {
 private:
  friend class ObjectRef;
  using HeapObjectRef::HeapObjectRef;
};

class JSGlobalProxyRef : public JSObjectRef {
 private:
  friend class ObjectRef;
  using JSObjectRef::JSObjectRef;
};

class CodeRef : public HeapObjectRef {
 private:
  friend class ObjectRef;
  using HeapObjectRef::HeapObjectRef;
};

class InternalizedStringRef : public StringRef {
 private:
  friend class ObjectRef;
  using StringRef::StringRef;
};

class V8_EXPORT_PRIVATE JSHeapBroker : public NON_EXPORTED_BASE(ZoneObject) {
 public:
  JSHeapBroker(Isolate* isolate, Zone* zone);

  // Creates the corresponding ObjectRef or returns the existing one.
  ObjectRef Serialize(Handle<Object> object);

  // Returns the corresponding ObjectRef, which must already exist.
  ObjectRef Ref(Handle<Object> object) const;

// Convenience wrappers around Ref.
#define DEFINE_REF(Name)                             \
  Name##Ref Name##Ref(Handle<Object> object) const { \
    return Ref(object).As##Name();                   \
  }
  HEAP_BROKER_OBJECT_LIST(DEFINE_REF)
#undef DEFINE_REF

  HeapObjectType HeapObjectTypeFromMap(Handle<Map> map) const {
    AllowHandleDereference handle_dereference;
    return HeapObjectTypeFromMap(*map);
  }

  static base::Optional<int> TryGetSmi(Handle<Object> object);

  Isolate* isolate() const { return isolate_; }

 private:
  friend class HeapObjectRef;
  HeapObjectType HeapObjectTypeFromMap(Map* map) const;

  Isolate* const isolate_;
  Zone* const zone_;
  mutable ZoneUnorderedMap<Address, Handle<Object>> refs_;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_JS_HEAP_BROKER_H_
