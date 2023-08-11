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

class HprofWriterImpl {
 public:
  HprofWriterImpl() = default;

  void Start() {
    time_ = static_cast<uint64_t>(
        V8::GetCurrentPlatform()->CurrentClockTimeMilliseconds());
    WriteFileHeader();
    WriteStackTrace(kFakeStackTraceSerial, 0);
    // TODO: write immortal objects / sentinels?
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
    classloads_.write_u1(static_cast<uint8_t>(Tag::kHeapDump));
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

#if V8_ENABLE_WEBASSEMBLY
  void AddWasmStruct(WasmStruct obj) {
    Isolate* isolate = obj->GetIsolate();
    wasm::StructType* type = obj->type();
    Tagged_t object_id = static_cast<Tagged_t>(obj->ptr());
    Tagged_t class_object = static_cast<Tagged_t>(obj->map()->ptr());

    // "Instance dump".
    heapdump_.write_u1(static_cast<uint8_t>(SubTag::kInstanceDump));
    heapdump_.write_ID(object_id);
    heapdump_.write_u4(kFakeStackTraceSerial);
    heapdump_.write_ID(class_object);
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
          Object value = obj->RawField(field_offset).load(isolate);
          // TODO: special handling for Smis?
          // TODO: special handling for strings?
          // TODO: special handling for null?
          // TODO: special handling for externref?
          heapdump_.write_ID(static_cast<Tagged_t>(value->ptr()));
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
    Isolate* isolate = obj->GetIsolate();
    wasm::ArrayType* type = obj->type();
    Tagged_t array_id = static_cast<Tagged_t>(obj->ptr());
    uint32_t num_elements = obj->length();
    wasm::ValueType element_type = type->element_type();
    bool is_reference = element_type.is_reference();
    SubTag tag =
        is_reference ? SubTag::kObjectArrayDump : SubTag::kPrimitiveArrayDump;

    // "Object / primitive array dump".
    heapdump_.write_u1(static_cast<uint8_t>(tag));
    heapdump_.write_ID(array_id);
    heapdump_.write_u4(kFakeStackTraceSerial);
    heapdump_.write_u4(num_elements);
    if (is_reference) {
      Tagged_t class_object = static_cast<Tagged_t>(obj->map()->ptr());
      heapdump_.write_ID(class_object);
    } else {
      heapdump_.write_u1(
          static_cast<uint8_t>(WasmTypeToBasicType(element_type)));
    }
    if (element_type.kind() == wasm::kS128) {
      // TODO(jkummerow): better support for S128.
      for (uint32_t i = 0; i < num_elements; i++) {
        heapdump_.write_u8(
            base::ReadUnalignedValue<uint64_t>(obj->ElementAddress(i)));
      }
    } else if (is_reference) {
      for (uint32_t i = 0; i < num_elements; i++) {
        Object value = obj->ElementSlot(i).load(isolate);
        // TODO: special handling for some values? (see struct case)
        heapdump_.write_ID(static_cast<Tagged_t>(value->ptr()));
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
    uint32_t string_id = NextOffheapStringId();
    WriteString(string_id, sb.start(), static_cast<uint32_t>(sb.length()));

    uint32_t class_serial = NextClassSerial();
    Tagged_t class_object_id = static_cast<Tagged_t>(map.ptr());
    WriteLoadClass(class_serial, class_object_id, kFakeStackTraceSerial,
                   string_id);

    // The {map} itself is written as a "Class dump".
    uint32_t instance_size_bytes = 0;
    Tagged_t super_class_object = 0;
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
        Map super_map = Map::cast(info->supertypes(supertype_index));
        super_class_object = static_cast<Tagged_t>(super_map->ptr());
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
        Map super_map = Map::cast(info->supertypes(supertype_index));
        super_class_object = static_cast<Tagged_t>(super_map->ptr());
      }
    }

    // TODO(jkummerow): Could these be useful for us?
    Tagged_t class_loader = 0;
    Tagged_t signers_object = 0;
    Tagged_t protection_domain_object = 0;

    heapdump_.write_u1(static_cast<uint8_t>(SubTag::kClassDump));
    heapdump_.write_ID(class_object_id);
    heapdump_.write_u4(kFakeStackTraceSerial);
    heapdump_.write_ID(super_class_object);
    heapdump_.write_ID(class_loader);
    heapdump_.write_ID(signers_object);
    heapdump_.write_ID(protection_domain_object);
    heapdump_.write_ID(0);  // reserved 1
    heapdump_.write_ID(0);  // reserved 2
    heapdump_.write_u4(instance_size_bytes);
    heapdump_.write_u2(0);  // Size of constant pool.
    heapdump_.write_u2(0);  // Number of static fields.
    heapdump_.write_u2(added_instance_fields);
    // This loop is never entered for Wasm array classes, because they have
    // field_count == super_field_count == 0. So accessing {struct_type}
    // inside the loop is safe.
    for (uint32_t i = field_count; i-- > super_field_count;) {
      sb.rewind_to_start();
      uint32_t field_name = NextOffheapStringId();
      names->PrintFieldName(sb, info->type_index(), i);
      WriteString(field_name, sb.start(), static_cast<uint32_t>(sb.length()));
      heapdump_.write_u4(field_name);
      heapdump_.write_u1(
          static_cast<uint8_t>(WasmTypeToBasicType(struct_type->field(i))));
    }
  }
#endif

 private:
  static const uint32_t kIdSize = kTaggedSize;
  static const uint32_t kFakeStackTraceSerial = 0;

  enum class Tag : uint8_t {
    kString = 0x01,
    kLoadClass = 0x02,
    kStackTrace = 0x05,
    kHeapDump = 0x0c,
  };

  enum class SubTag : uint8_t {
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

  void WriteString(Tagged_t id, const char* str) {
    WriteString(id, str, static_cast<uint32_t>(strlen(str)));
  }

  void WriteString(Tagged_t id, const char* str, uint32_t str_length) {
    strings_.write_u1(static_cast<uint8_t>(Tag::kString));
    strings_.write_u4(0);  // time
    BytesThatFollow length(strings_);
    strings_.write_ID(id);
    strings_.write(str, str_length);
  }

  void WriteLoadClass(uint32_t serial, Tagged_t object_id,
                      uint32_t stack_trace_serial, Tagged_t name) {
    classloads_.write_u1(static_cast<uint8_t>(Tag::kLoadClass));
    classloads_.write_u4(0);  // time
    BytesThatFollow length(classloads_);
    classloads_.write_u4(serial);
    classloads_.write_ID(object_id);
    classloads_.write_u4(stack_trace_serial);
    classloads_.write_ID(name);
  }

  void WriteStackTrace(uint32_t serial, uint32_t thread_serial) {
    strings_.write_u1(static_cast<uint8_t>(Tag::kStackTrace));
    strings_.write_u4(0);  // time
    BytesThatFollow length(strings_);
    strings_.write_u4(serial);
    strings_.write_u4(thread_serial);
    strings_.write_u4(0);  // number of frames
  }

  uint32_t NextOffheapStringId() {
    // Produce only even numbers so they don't collide with heap objects.
    last_string_id_ += 2;
    return last_string_id_;
  }
  // Per spec, must return values > 0.
  uint32_t NextClassSerial() { return ++last_class_serial_; }

  FileContentsBuilder strings_;
  FileContentsBuilder classloads_;
  FileContentsBuilder heapdump_;
  uint64_t time_;
  uint32_t last_string_id_ = 0;
  uint32_t last_class_serial_ = 0;
};

HprofWriter::HprofWriter()
    : impl_(v8_flags.wasm_hprof ? new HprofWriterImpl() : nullptr) {}

HprofWriter::~HprofWriter() = default;

void HprofWriter::Start() { impl_->Start(); }
void HprofWriter::Finish() { impl_->Finish(); }
void HprofWriter::AddWasmStruct(WasmStruct obj) { impl_->AddWasmStruct(obj); }
void HprofWriter::AddWasmArray(WasmArray obj) { impl_->AddWasmArray(obj); }
void HprofWriter::AddWasmMap(Map map) { impl_->AddWasmMap(map); }

}  // namespace v8::internal
