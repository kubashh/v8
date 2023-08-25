// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/profiler/hprof-writer.h"

#include <inttypes.h>
#include <stdint.h>

#include <fstream>
#include <vector>

#include "src/objects/heap-object-inl.h"
#include "src/objects/js-objects-inl.h"

#if V8_ENABLE_WEBASSEMBLY
#include "src/wasm/names-provider.h"
#include "src/wasm/string-builder.h"
#include "src/wasm/wasm-code-manager.h"
#include "src/wasm/wasm-objects-inl.h"
#endif  // V8_ENABLE_WEBASSEMBLY

// Spec:
// https://hg.openjdk.org/jdk8/jdk8/jdk/raw-file/tip/src/share/demo/jvmti/hprof/manual.html#mozTocId848088

namespace v8::internal {

enum class Tag : uint8_t {
  kString = 0x01,
  kLoadClass = 0x02,
  kStackTrace = 0x05,
  kHeapDump = 0x0c,
};

enum class SubTag : uint8_t {
  kUnknownRoot = 0xFF,
  kFrameRoot = 0x03,
  kClassDump = 0x20,
  kInstanceDump = 0x21,
  kObjectArrayDump = 0x22,
  kPrimitiveArrayDump = 0x23,
};

enum class BasicType : uint8_t {
  kObject = 2,
  kBoolean = 4,
  kChar = 5,
  kFloat = 6,
  kDouble = 7,
  kByte = 8,
  kShort = 9,
  kInt = 10,
  kLong = 11,
};

class FileContentsBuilder {
 public:
  FileContentsBuilder() { AddChunk(kChunkSize); }

  ~FileContentsBuilder() {
    for (char* chunk : chunks_) delete[] chunk;
  }

  char* allocate(size_t n) {
    if (remaining_bytes_ < n) Grow(n);
    char* result = cursor_;
    cursor_ += n;
    total_ += n;
    remaining_bytes_ -= n;
    return result;
  }

  void write(const char* data, size_t n) { memcpy(allocate(n), data, n); }
  void write_u1(uint8_t u1) { *allocate(1) = u1; }
#if V8_TARGET_BIG_ENDIAN
  void write_u2(uint16_t u2) { memcpy(allocate(2), &u2, 2); }
  void write_u4(uint32_t u4) { memcpy(allocate(4), &u4, 4); }
  void write_u8(uint64_t u8) { memcpy(allocate(8), &u8, 8); }
#else   // V8_TARGET_BIG_ENDIAN
  void write_u2(uint16_t u2) {
    u2 = ByteReverse16(u2);
    memcpy(allocate(2), &u2, 2);
  }
  void write_u4(uint32_t u4) {
    u4 = ByteReverse32(u4);
    memcpy(allocate(4), &u4, 4);
  }
  void write_u8(uint64_t u8) {
    u8 = ByteReverse64(u8);
    memcpy(allocate(8), &u8, 8);
  }
#endif  // V8_TARGET_BIG_ENDIAN

  void write_ID(Tagged_t id) {
    if constexpr (kTaggedSize == 4) {
      write_u4(id);
    } else {
      write_u8(id);
    }
  }

  void write_ID(HeapObject obj) {
#if V8_COMPRESS_POINTERS
    write_ID(V8HeapCompressionScheme::CompressAny(obj->ptr()));
#else
    write_ID(static_cast<Tagged_t>(obj->ptr()));
#endif
  }

  void write_Object(Object obj) {
    if (IsSmi(obj)) {
      // Map all Smis to 0 to avoid accidental clashes with (fake) objects
      // elsewhere in the dump.
      write_ID(Tagged_t{0});
    } else {
      write_ID(HeapObject::cast(obj));
    }
  }

  void write_tag(Tag tag) { write_u1(static_cast<uint8_t>(tag)); }
  void write_tag(SubTag tag) { write_u1(static_cast<uint8_t>(tag)); }
  void write_type(BasicType type) { write_u1(static_cast<uint8_t>(type)); }

  void WriteToFile(std::ofstream& f) {
    FinishChunk();
    for (char* chunk : chunks_) {
      size_t chunk_size = *(reinterpret_cast<size_t*>(chunk));
      f.write(chunk + kHeaderSize, chunk_size);
    }
  }

  size_t total() { return total_; }

 private:
  static constexpr size_t kChunkSize = 1024 * 1024;
  static constexpr size_t kHeaderSize = sizeof(size_t);

  void AddChunk(size_t min_size) {
    min_size += kHeaderSize;
    size_t chunk_size = min_size < kChunkSize ? kChunkSize : min_size;
    char* new_chunk = new char[chunk_size];
    chunks_.push_back(new_chunk);
    start_ = new_chunk + kHeaderSize;
    cursor_ = start_;
    remaining_bytes_ = chunk_size - kHeaderSize;
  }

  void FinishChunk() {
    size_t* chunk_start = reinterpret_cast<size_t*>(start_ - kHeaderSize);
    *chunk_start = (cursor_ - start_);
  }

  void Grow(size_t requested) {
    FinishChunk();
    AddChunk(requested);
  }

  std::vector<char*> chunks_;
  size_t total_ = 0;
  char* start_;
  char* cursor_;
  size_t remaining_bytes_;
};

class BytesThatFollow {
 public:
  explicit BytesThatFollow(FileContentsBuilder& builder) : builder_(builder) {
    size_position_ = builder.allocate(4);
    total_ = builder.total();  // After {allocate(4)}!
  }
  ~BytesThatFollow() {
    uint32_t size = static_cast<uint32_t>(builder_.total() - total_);
#if !V8_TARGET_BIG_ENDIAN
    size = ByteReverse32(size);
#endif
    memcpy(size_position_, &size, 4);
  }

 private:
  FileContentsBuilder& builder_;
  size_t total_;
  char* size_position_;
};

class StringManager : public FileContentsBuilder {
 public:
  explicit StringManager(Isolate* isolate)
      : FileContentsBuilder(), isolate_(isolate) {}

  Tagged_t AddName(Name name) {
    if (IsString(name)) return Add(String::cast(name));
    Symbol sym = Symbol::cast(name);
    if (!IsString(sym.description())) return Add("<symbol>");
    String desc = String::cast(sym->description());
    int length =
        std::min(v8_flags.heap_snapshot_string_limit.value(), desc->length());
    std::unique_ptr<char[]> raw = desc->ToCString(
        DISALLOW_NULLS, ROBUST_STRING_TRAVERSAL, 0, length, &length);
    if (sym->is_private_name()) {
      return Add(raw.get(), static_cast<uint32_t>(length));
    }
    int str_length = 8 + length + 2;  // For '<symbol ' and '>\0'.
    std::unique_ptr<char[]> str(new char[str_length]);
    snprintf(str.get(), str_length, "<symbol %s>", raw.get());
    return Add(str.get(), static_cast<uint32_t>(str_length - 1));
  }

  Tagged_t Add(String string) {
    int length;
    std::unique_ptr<char[]> raw =
        string->ToCString(DISALLOW_NULLS, ROBUST_STRING_TRAVERSAL, &length);
    return Add(raw.get(), static_cast<uint32_t>(length));
  }

  Tagged_t Add(const char* string) {
    return Add(string, static_cast<uint32_t>(strlen(string)));
  }

  Tagged_t Add(const char* string, uint32_t length) {
    // TODO(jkummerow): It would be nice to deduplicate strings.
    // We could even be fancy with a custom map which reuses the actual
    // written bytes when comparing existing entries, instead of storing copies.
    // But until we see a concrete need for deduplication, let's not
    // over-engineer things.
    Tagged_t id = NextOffheapStringId();
    write_tag(Tag::kString);
    write_u4(0);  // time
    BytesThatFollow bytes(*this);
    write_ID(id);
    write(string, length);
    return id;
  }

  Tagged_t JSObjectMapName(Map map) {
    // This is modeled after {JSReceiver::GetConstructorName}.
    InstanceType instance_type = map->instance_type();
    if (!InstanceTypeChecker::IsJSProxy(instance_type) &&
        map->new_target_is_base() && !map->is_prototype_map()) {
      Tagged_t maybe_id = ConstructorName(map->GetConstructor());
      if (maybe_id != 0) return maybe_id;
    }
    for (PrototypeIterator it(isolate_, map); !it.IsAtEnd();
         it.AdvanceIgnoringProxies()) {
      Handle<JSReceiver> curr = PrototypeIterator::GetCurrent<JSReceiver>(it);
      LookupIterator it_to_string_tag(
          isolate_, curr, isolate_->factory()->to_string_tag_symbol(),
          LookupIterator::OWN_SKIP_INTERCEPTOR);
      Handle<Object> maybe_to_string = JSReceiver::GetDataProperty(
          &it_to_string_tag, AllocationPolicy::kAllocationDisallowed);
      if (IsString(*maybe_to_string)) {
        return Add(String::cast(*maybe_to_string));
      }
      LookupIterator it_constructor(isolate_, curr,
                                    isolate_->factory()->constructor_string(),
                                    LookupIterator::OWN_SKIP_INTERCEPTOR);
      Handle<Object> maybe_constructor = JSReceiver::GetDataProperty(
          &it_constructor, AllocationPolicy::kAllocationDisallowed);
      Tagged_t maybe_constructor_id = ConstructorName(*maybe_constructor);
      if (maybe_constructor_id != 0) return maybe_constructor_id;
    }
    // Fall back to generic names. This is modeled after
    // {JSReceiver::class_name}, but for Maps as input and C strings as output.
#define CASE(Type, Name)                              \
  if (InstanceTypeChecker::Is##Type(instance_type)) { \
    return Add(Name);                                 \
  }
    CASE(JSFunctionOrBoundFunctionOrWrappedFunction, "Function")
    CASE(JSArgumentsObject, "Arguments")
    CASE(JSArray, "Array")
    // Would be nice to be able to distinguish shared ABs, but how?
    CASE(JSArrayBuffer, "ArrayBuffer")
    CASE(JSArrayIterator, "ArrayIterator")
    CASE(JSDate, "Date");
    CASE(JSError, "Error");
    CASE(JSGeneratorObject, "Generator");
    CASE(JSMap, "Map");
    CASE(JSMapIterator, "MapIterator");
    if (InstanceTypeChecker::IsJSProxy(instance_type)) {
      return map->is_callable() ? Add("Function") : Add("Object");
    }
    CASE(JSRegExp, "RegExp")
    CASE(JSSet, "Set")
    CASE(JSSetIterator, "SetIterator")
    if (InstanceTypeChecker::IsJSTypedArray(instance_type)) {
#define KIND(Type, type, TYPE, ctype)            \
  if (map->elements_kind() == TYPE##_ELEMENTS) { \
    return Add(#Type "Array");                   \
  }
      TYPED_ARRAYS(KIND)
#undef KIND
    }
    CASE(JSPrimitiveWrapper, "PrimitiveWrapper")  // We can't know which kind.
    CASE(JSWeakMap, "WeakMap")
    CASE(JSWeakSet, "WeakSet")
    CASE(JSGlobalProxy, "global")
    CASE(JSSharedStruct, "SharedStruct")
    CASE(JSSharedArray, "SharedArray")
    CASE(JSAtomicsMutex, "AtomicsMutex")
    CASE(JSAtomicsCondition, "AtomicsCondition")
#undef CASE

    // If all else fails, it's "an object".
    return Add("Object");
  }

 private:
  Tagged_t ConstructorName(Object maybe_constructor) {
    if (!IsJSFunction(maybe_constructor)) return 0;
    JSFunction constructor = JSFunction::cast(maybe_constructor);
    // Adapted version of SharedFunctionInfo::DebugName().
    SharedFunctionInfo sfi = constructor->shared();
    // TODO(jkummerow): Do we need to handle exported Wasm functions here?
    // TODO(jkummerow): Do we need to handle class member initializer functions?
    // They're both unlikely to be used as JS constructors.

    String name = sfi->Name();
    if (name->length() == 0) name = sfi->inferred_name();
    if (name->length() == 0 ||
        name->Equals(ReadOnlyRoots(isolate_).Object_string())) {
      return 0;
    }
    return Add(name);
  }

  Tagged_t NextOffheapStringId() {
    // Produce only even numbers so they don't collide with heap objects.
    last_string_id_ += 2;
    return last_string_id_;
  }

  Isolate* isolate_;
  Tagged_t last_string_id_ = 0;
};

class HprofWriterImpl {
 public:
  explicit HprofWriterImpl(Isolate* isolate)
      : isolate_(isolate), strings_(isolate) {}

  void Start() {
    time_ = static_cast<uint64_t>(
        V8::GetCurrentPlatform()->CurrentClockTimeMilliseconds());
    WriteFileHeader();
    WriteStackTrace(kFakeStackTraceSerial, 0);
  }

  bool Finish() {
    base::EmbeddedVector<char, 128> buffer;
    base::SNPrintF(buffer, "v8-wasm-heapdump-%" PRIu64 ".hprof", time_);
    const char* filename = buffer.cbegin();
    std::ofstream f(filename, std::ios::out | std::ios::binary);
    if (!f.is_open()) {
      std::cerr << "Error: could not open " << filename << "\n";
      return false;
    }
    strings_.WriteToFile(f);
    // Append the section header for {heapdump_} to {classloads_}.
    classloads_.write_tag(Tag::kHeapDump);
    classloads_.write_u4(0);  // time
    size_t heap_size = heapdump_.total();
    if (heap_size > std::numeric_limits<uint32_t>::max()) {
      // TODO(jkummerow): Use "heap dump segment" tags to avoid this limit.
      std::cerr << "Error: heap dump is too big ( " << heap_size << " bytes)\n";
      return false;
    }
    classloads_.write_u4(static_cast<uint32_t>(heap_size));
    classloads_.WriteToFile(f);
    heapdump_.WriteToFile(f);
    return true;
  }

  void AddRoot(HeapObject obj, Root root) {
    if (root == Root::kStackRoots) {
      heapdump_.write_tag(SubTag::kFrameRoot);
      heapdump_.write_ID(obj);
      heapdump_.write_u4(0);  // thread serial number
      heapdump_.write_u4(0);  // frame number
    } else {
      // TODO(jkummerow): Are any other kinds of roots useful to distinguish?
      heapdump_.write_tag(SubTag::kUnknownRoot);
      heapdump_.write_ID(obj);
    }
  }

  void AddHeapObject(HeapObject object, InstanceType instance_type) {
    if (InstanceTypeChecker::IsJSObject(instance_type)) {
      AddJSObject(JSObject::cast(object));
    } else if (InstanceTypeChecker::IsMap(instance_type)) {
      Map map = Map::cast(object);
      if (InstanceTypeChecker::IsJSObject(map->instance_type())) {
        AddJSObjectMap(map);
#if V8_ENABLE_WEBASSEMBLY
      } else if (InstanceTypeChecker::IsWasmObject(map->instance_type())) {
        AddWasmMap(map);
#endif
      } else {
        AddOtherMap(map);
      }
    } else if (InstanceTypeChecker::IsFixedArray(instance_type)) {
      AddFixedArray(FixedArray::cast(object));
    } else if (InstanceTypeChecker::IsFixedDoubleArray(instance_type)) {
      AddFixedDoubleArray(FixedDoubleArray::cast(object));
    } else if (InstanceTypeChecker::IsString(instance_type)) {
      AddString(String::cast(object));

#if V8_ENABLE_WEBASSEMBLY
    } else if (InstanceTypeChecker::IsWasmStruct(instance_type)) {
      AddWasmStruct(WasmStruct::cast(object));
    } else if (InstanceTypeChecker::IsWasmArray(instance_type)) {
      AddWasmArray(WasmArray::cast(object));
#endif
    }
  }

 private:
  static const uint32_t kIdSize = kTaggedSize;
  static const uint32_t kFakeStackTraceSerial = 0;

  void AddJSObject(JSObject obj) {
    Map map = obj->map();
    // "Instance dump".
    heapdump_.write_tag(SubTag::kInstanceDump);
    heapdump_.write_ID(obj);
    heapdump_.write_u4(kFakeStackTraceSerial);
    heapdump_.write_ID(obj->map());
    BytesThatFollow length(heapdump_);
    for (int i = map->GetInObjectProperties(); i >= 0; i--) {
      heapdump_.write_Object(obj.InObjectPropertyAt(i));
    }
    heapdump_.write_ID(obj->elements());
    heapdump_.write_Object(obj->raw_properties_or_hash());
  }

  void AddJSObjectMap(Map map) {
    Tagged_t name_string_id = strings_.JSObjectMapName(map);

    uint32_t instance_size_bytes = map->instance_size();
    DCHECK_NE(instance_size_bytes, kVariableSizeSentinel);
    // We could consider different ways of approximating a Java-style
    // subtyping hierarchy, e.g. using the transition tree or taking
    // prototypes into account. For now, we simply pretend that every
    // JSObject map derives straight from the basic JSObject map.
    Map super_map = isolate_->object_function()->initial_map();
    uint16_t added_instance_fields = map->GetInObjectProperties();

    WriteClassDumpHeader(map, name_string_id, super_map, instance_size_bytes,
                         added_instance_fields);

    DescriptorArray descriptors = map->instance_descriptors();
    uint16_t inobject_properties_found = 0;
    for (int i = map->NumberOfOwnDescriptors(); i >= 0; i--) {
      InternalIndex ii(i);
      FieldIndex index = FieldIndex::ForDescriptor(map, ii);
      if (index.is_inobject()) {
        inobject_properties_found++;
        Name field_name = descriptors->GetKey(ii);
        Tagged_t field_name_id = strings_.AddName(field_name);
        heapdump_.write_ID(field_name_id);
        heapdump_.write_type(BasicType::kObject);
      }
    }
    DCHECK_EQ(added_instance_fields, inobject_properties_found);
    USE(inobject_properties_found);
  }

  void AddOtherMap(Map map) {
    DCHECK(!InstanceTypeChecker::IsJSObject(map));
    DCHECK(!InstanceTypeChecker::IsWasmObject(map));
    Tagged_t name_string_id = 0;
    uint32_t instance_size = map->instance_size();
    Map super_map;
    uint16_t added_instance_fields = 0;

    // Handle a few special cases.
    if (map == isolate_->object_function()->initial_map()) {
      name_string_id = strings_.Add("Object");
      added_instance_fields = 2;  // Properties, elements.
    } else if (map == *isolate_->factory()->fixed_array_map()) {
      name_string_id = strings_.Add("FixedArray");
    }

    WriteClassDumpHeader(map, name_string_id, super_map, instance_size,
                         added_instance_fields);

    if (added_instance_fields > 0) {
      // This currently only happens for one of the special cases above, so
      // we know exactly which fields to add.
      Tagged_t elements_string = strings_.Add("<elements>");
      Tagged_t properties_string = strings_.Add("<properties>");
      heapdump_.write_ID(elements_string);
      heapdump_.write_type(BasicType::kObject);
      heapdump_.write_ID(properties_string);
      heapdump_.write_type(BasicType::kObject);
    }
  }

  void AddFixedArray(FixedArray array) {
    int length = array->length();
    heapdump_.write_tag(SubTag::kObjectArrayDump);
    heapdump_.write_ID(array);
    heapdump_.write_u4(kFakeStackTraceSerial);
    heapdump_.write_u4(length);
    heapdump_.write_ID(array->map());
    for (int i = 0; i < length; i++) {
      heapdump_.write_Object(array->get(i));
    }
  }

  void AddFixedDoubleArray(FixedDoubleArray array) {
    int length = array->length();
    heapdump_.write_tag(SubTag::kPrimitiveArrayDump);
    heapdump_.write_ID(array);
    heapdump_.write_u4(kFakeStackTraceSerial);
    heapdump_.write_u4(length);
    heapdump_.write_type(BasicType::kDouble);
    for (int i = 0; i < length; i++) {
      heapdump_.write_u8(array->get_representation(i));
    }
  }

  void AddString(String string) {
    // TODO
  }

#if V8_ENABLE_WEBASSEMBLY
  void AddWasmStruct(WasmStruct obj) {
    wasm::StructType* type = obj->type();

    // "Instance dump".
    heapdump_.write_tag(SubTag::kInstanceDump);
    heapdump_.write_ID(obj);
    heapdump_.write_u4(kFakeStackTraceSerial);
    heapdump_.write_ID(obj->map());
    BytesThatFollow length(heapdump_);
    for (uint32_t i = type->field_count(); i-- > 0;) {
      wasm::ValueType field_type = type->field(i);
      int field_offset = type->field_offset(i);
      Address field_address = obj->RawFieldAddress(field_offset);
      switch (field_type.kind()) {
        case wasm::kF32:
        case wasm::kI32:
          heapdump_.write_u4(base::ReadUnalignedValue<uint32_t>(field_address));
          break;
        case wasm::kF64:
        case wasm::kI64:
          heapdump_.write_u8(base::ReadUnalignedValue<uint64_t>(field_address));
          break;
        case wasm::kI8:
          heapdump_.write_u1(base::ReadUnalignedValue<uint8_t>(field_address));
          break;
        case wasm::kI16:
          heapdump_.write_u2(base::ReadUnalignedValue<uint16_t>(field_address));
          break;
        case wasm::kRef:
        case wasm::kRefNull: {
          Object value = obj->RawField(field_offset).load(isolate_);
          heapdump_.write_Object(value);
          break;
        }
        case wasm::kS128:
          // TODO(jkummerow): Better support for S128.
          DCHECK_EQ(BasicType::kLong, WasmTypeToBasicType(wasm::kWasmS128));
          heapdump_.write_u8(base::ReadUnalignedValue<uint64_t>(field_address));
          break;
        case wasm::kRtt:
        case wasm::kVoid:
        case wasm::kBottom:
          UNREACHABLE();
      }
    }
  }

  void AddWasmArray(WasmArray obj) {
    wasm::ArrayType* type = obj->type();
    uint32_t num_elements = obj->length();
    wasm::ValueType element_type = type->element_type();
    bool is_reference = element_type.is_reference();
    SubTag tag =
        is_reference ? SubTag::kObjectArrayDump : SubTag::kPrimitiveArrayDump;

    // "Object / primitive array dump".
    heapdump_.write_tag(tag);
    heapdump_.write_ID(obj);
    heapdump_.write_u4(kFakeStackTraceSerial);
    heapdump_.write_u4(num_elements);
    if (is_reference) {
      heapdump_.write_ID(obj->map());
    } else {
      heapdump_.write_type(WasmTypeToBasicType(element_type));
    }
    if (element_type.kind() == wasm::kS128) {
      // TODO(jkummerow): better support for S128.
      for (uint32_t i = 0; i < num_elements; i++) {
        heapdump_.write_u8(
            base::ReadUnalignedValue<uint64_t>(obj->ElementAddress(i)));
      }
    } else if (is_reference) {
      for (uint32_t i = 0; i < num_elements; i++) {
        Object value = obj->ElementSlot(i).load(isolate_);
        heapdump_.write_Object(value);
      }
    } else {
      uint32_t total_size = element_type.value_kind_size() * num_elements;
      void* elem_start = reinterpret_cast<void*>(obj->ElementAddress(0));
#if V8_TARGET_BIG_ENDIAN
      memcpy(heapdump_.allocate(total_size), elem_start, total_size);
#else   // V8_TARGET_BIG_ENDIAN
      MemCopyAndSwitchEndianness(heapdump_.allocate(total_size), elem_start,
                                 num_elements, element_type.value_kind_size());
#endif  // V8_TARGET_BIG_ENDIAN
    }
  }

  void AddWasmMap(Map map) {
    WasmTypeInfo info = map->wasm_type_info();
    WasmInstanceObject instance = WasmInstanceObject::cast(info->instance());
    wasm::NamesProvider* names =
        instance->module_object()->native_module()->GetNamesProvider();
    wasm::StringBuilder sb;
    names->PrintTypeName(sb, info->type_index());
    Tagged_t string_id =
        strings_.Add(sb.start(), static_cast<uint32_t>(sb.length()));

    uint32_t instance_size_bytes = 0;
    Map super_map;
    uint32_t super_field_count = 0;
    uint32_t field_count = 0;
    // "Added" meaning: not including superclass fields.
    uint16_t added_instance_fields = 0;
    wasm::StructType* struct_type = nullptr;
    int supertype_index = info->supertypes_length() - 1;
    while (supertype_index >= 0 &&
           IsUndefined(info->supertypes(supertype_index))) {
      supertype_index--;
    }
    if (map->instance_type() == WASM_STRUCT_TYPE) {
      struct_type = WasmStruct::type(map);
      instance_size_bytes = WasmStruct::Size(struct_type);
      if (supertype_index >= 0) {
        super_map = Map::cast(info->supertypes(supertype_index));
        super_field_count = WasmStruct::type(super_map)->field_count();
      }
      field_count = struct_type->field_count();
      static_assert(
          wasm::kV8MaxWasmStructFields < std::numeric_limits<uint16_t>::max(),
          "use a saturating cast below if this changes");
      added_instance_fields =
          static_cast<uint16_t>(field_count - super_field_count);
    } else {
      DCHECK_EQ(map->instance_type(), WASM_ARRAY_TYPE);
      if (supertype_index >= 0) {
        super_map = Map::cast(info->supertypes(supertype_index));
      }
    }

    WriteClassDumpHeader(map, string_id, super_map, instance_size_bytes,
                         added_instance_fields);

    // This loop is never entered for Wasm array classes, because they have
    // field_count == super_field_count == 0. So accessing {struct_type}
    // inside the loop is safe.
    for (uint32_t i = field_count; i-- > super_field_count;) {
      sb.rewind_to_start();
      names->PrintFieldName(sb, info->type_index(), i);
      Tagged_t field_name =
          strings_.Add(sb.start(), static_cast<uint32_t>(sb.length()));
      heapdump_.write_ID(field_name);
      heapdump_.write_type(WasmTypeToBasicType(struct_type->field(i)));
    }
  }

  BasicType WasmTypeToBasicType(wasm::ValueType type) {
    switch (type.kind()) {
      case wasm::kI32:
        return BasicType::kInt;
      case wasm::kI64:
        return BasicType::kLong;
      case wasm::kF32:
        return BasicType::kFloat;
      case wasm::kF64:
        return BasicType::kDouble;
      case wasm::kI8:
        return BasicType::kByte;
      case wasm::kI16:
        return BasicType::kShort;
      case wasm::kRef:
      case wasm::kRefNull:
        return BasicType::kObject;
      case wasm::kS128:
        // TODO(jkummerow): This is a lie, but what choice do we have?
        // We could pretend that there are *two* fields of 64 bits each,
        // at the cost of significantly more complex field iteration logic.
        return BasicType::kLong;
      case wasm::kRtt:
      case wasm::kVoid:
      case wasm::kBottom:
        UNREACHABLE();
    }
  }
#endif

  void WriteFileHeader() {
    static constexpr char kMagic[] = "JAVA PROFILE 1.0.2";
    strings_.write(kMagic, strlen(kMagic));
    strings_.write_u1(0);
    strings_.write_u4(kIdSize);
    uint32_t timestamp_high = static_cast<uint32_t>(time_ >> 32);
    uint32_t timestamp_low = static_cast<uint32_t>(time_);
    strings_.write_u4(timestamp_high);
    strings_.write_u4(timestamp_low);
  }

  void WriteLoadClass(uint32_t serial, Map object,
                      uint32_t stack_trace_serial, Tagged_t name) {
    classloads_.write_tag(Tag::kLoadClass);
    classloads_.write_u4(0);  // time
    BytesThatFollow length(classloads_);
    classloads_.write_u4(serial);
    classloads_.write_ID(object);
    classloads_.write_u4(stack_trace_serial);
    classloads_.write_ID(name);
  }

  void WriteStackTrace(uint32_t serial, uint32_t thread_serial) {
    strings_.write_tag(Tag::kStackTrace);
    strings_.write_u4(0);  // time
    BytesThatFollow length(strings_);
    strings_.write_u4(serial);
    strings_.write_u4(thread_serial);
    strings_.write_u4(0);  // number of frames
  }

  void WriteClassDumpHeader(Map map, Tagged_t name_string_id, Map super_map,
                            uint32_t instance_size_bytes,
                            uint16_t added_instance_fields) {
    uint32_t class_serial = NextClassSerial();
    WriteLoadClass(class_serial, map, kFakeStackTraceSerial, name_string_id);

    heapdump_.write_tag(SubTag::kClassDump);
    heapdump_.write_ID(map);
    heapdump_.write_u4(kFakeStackTraceSerial);
    heapdump_.write_ID(super_map);
    heapdump_.write_ID(0);  // Class loader.
    heapdump_.write_ID(0);  // Signers object.
    heapdump_.write_ID(0);  // Protection domain object.
    heapdump_.write_ID(0);  // reserved 1
    heapdump_.write_ID(0);  // reserved 2
    heapdump_.write_u4(instance_size_bytes);
    heapdump_.write_u2(0);  // Size of constant pool.
    heapdump_.write_u2(0);  // Number of static fields.
    heapdump_.write_u2(added_instance_fields);
    // Callers must write class-specific fields next.
  }

  // Per spec, must return values > 0.
  uint32_t NextClassSerial() { return ++last_class_serial_; }

  Isolate* isolate_;
  StringManager strings_;
  FileContentsBuilder classloads_;
  FileContentsBuilder heapdump_;
  uint64_t time_;
  uint32_t last_class_serial_ = 0;
};

HprofWriter::HprofWriter(Isolate* isolate)
    : impl_(v8_flags.wasm_hprof ? new HprofWriterImpl(isolate) : nullptr) {}

HprofWriter::~HprofWriter() = default;

void HprofWriter::Start() { impl_->Start(); }
void HprofWriter::Finish() { impl_->Finish(); }
void HprofWriter::AddRoot(HeapObject obj, Root root) {
  impl_->AddRoot(obj, root);
}
void HprofWriter::AddHeapObject(HeapObject obj, InstanceType instance_type) {
  impl_->AddHeapObject(obj, instance_type);
}

}  // namespace v8::internal
