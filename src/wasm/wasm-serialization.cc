// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/wasm-serialization.h"

#include "src/assembler-inl.h"
#include "src/base/safe_conversions.h"
#include "src/code-stubs.h"
#include "src/external-reference-table.h"
#include "src/objects-inl.h"
#include "src/objects.h"
#include "src/snapshot/code-serializer.h"
#include "src/snapshot/serializer-common.h"
#include "src/utils.h"
#include "src/version.h"
#include "src/wasm/function-compiler.h"
#include "src/wasm/module-compiler.h"
#include "src/wasm/module-decoder.h"
#include "src/wasm/wasm-code-manager.h"
#include "src/wasm/wasm-module.h"
#include "src/wasm/wasm-objects-inl.h"
#include "src/wasm/wasm-objects.h"
#include "src/wasm/wasm-result.h"

namespace v8 {
namespace internal {
namespace wasm {

namespace {

// TODO(bbudge) Try to unify the various implementations of readers and writers
// in WASM, e.g. StreamProcessor and ZoneBuffer, with these.
class Writer {
 public:
  explicit Writer(Vector<byte> buffer)
      : start_(buffer.start()), end_(buffer.end()), pos_(buffer.start()) {}

  size_t bytes_written() const { return pos_ - start_; }
  byte* current_location() const { return pos_; }
  size_t current_size() const { return end_ - pos_; }
  Vector<byte> current_buffer() const {
    return {current_location(), current_size()};
  }

  template <typename T>
  void Write(const T& value) {
    DCHECK_GE(current_size(), sizeof(T));
    WriteUnalignedValue(reinterpret_cast<Address>(current_location()), value);
    pos_ += sizeof(T);
    if (FLAG_wasm_trace_serialization) {
      OFStream os(stdout);
      os << "wrote: " << (size_t)value << " sized: " << sizeof(T) << std::endl;
    }
  }

  void WriteVector(const Vector<const byte> v) {
    DCHECK_GE(current_size(), v.size());
    if (v.size() > 0) {
      memcpy(current_location(), v.start(), v.size());
      pos_ += v.size();
    }
    if (FLAG_wasm_trace_serialization) {
      OFStream os(stdout);
      os << "wrote vector of " << v.size() << " elements" << std::endl;
    }
  }

  void Skip(size_t size) { pos_ += size; }

 private:
  byte* const start_;
  byte* const end_;
  byte* pos_;
};

class Reader {
 public:
  explicit Reader(Vector<const byte> buffer)
      : start_(buffer.start()), end_(buffer.end()), pos_(buffer.start()) {}

  size_t bytes_read() const { return pos_ - start_; }
  const byte* current_location() const { return pos_; }
  size_t current_size() const { return end_ - pos_; }
  Vector<const byte> current_buffer() const {
    return {current_location(), current_size()};
  }

  template <typename T>
  T Read() {
    DCHECK_GE(current_size(), sizeof(T));
    T value =
        ReadUnalignedValue<T>(reinterpret_cast<Address>(current_location()));
    pos_ += sizeof(T);
    if (FLAG_wasm_trace_serialization) {
      OFStream os(stdout);
      os << "read: " << (size_t)value << " sized: " << sizeof(T) << std::endl;
    }
    return value;
  }

  void ReadVector(Vector<byte> v) {
    if (v.size() > 0) {
      DCHECK_GE(current_size(), v.size());
      memcpy(v.start(), current_location(), v.size());
      pos_ += v.size();
    }
    if (FLAG_wasm_trace_serialization) {
      OFStream os(stdout);
      os << "read vector of " << v.size() << " elements" << std::endl;
    }
  }

  void Skip(size_t size) { pos_ += size; }

 private:
  const byte* const start_;
  const byte* const end_;
  const byte* pos_;
};

constexpr size_t kVersionSize = 4 * sizeof(uint32_t);

// Start from 1 so an encoded stub id is not confused with an encoded builtin.
constexpr int kFirstStubId = 1;

void WriteVersion(Isolate* isolate, Writer* writer) {
  writer->Write(SerializedData::ComputeMagicNumber(
      isolate->heap()->external_reference_table()));
  writer->Write(Version::Hash());
  writer->Write(static_cast<uint32_t>(CpuFeatures::SupportedFeatures()));
  writer->Write(FlagList::Hash());
}

bool IsSupportedVersion(Isolate* isolate, const Vector<const byte> version) {
  if (version.size() < kVersionSize) return false;
  byte current_version[kVersionSize];
  Writer writer({current_version, kVersionSize});
  WriteVersion(isolate, &writer);
  return memcmp(version.start(), current_version, kVersionSize) == 0;
}

// On Intel, call sites are encoded as a displacement. For linking and for
// serialization/deserialization, we want to store/retrieve a tag (the function
// index). On Intel, that means accessing the raw displacement. Everywhere else,
// that simply means accessing the target address.
void SetWasmCalleeTag(RelocInfo* rinfo, uint32_t tag) {
#if V8_TARGET_ARCH_X64 || V8_TARGET_ARCH_IA32
  *(reinterpret_cast<uint32_t*>(rinfo->target_address_address())) = tag;
#else
  Address addr = static_cast<Address>(tag);
  if (rinfo->rmode() == RelocInfo::EXTERNAL_REFERENCE) {
    rinfo->set_target_external_reference(addr, SKIP_ICACHE_FLUSH);
  } else {
    rinfo->set_target_address(addr, SKIP_WRITE_BARRIER, SKIP_ICACHE_FLUSH);
  }
#endif
}

uint32_t GetWasmCalleeTag(RelocInfo* rinfo) {
#if V8_TARGET_ARCH_X64 || V8_TARGET_ARCH_IA32
  return *(reinterpret_cast<uint32_t*>(rinfo->target_address_address()));
#else
  Address addr = rinfo->rmode() == RelocInfo::EXTERNAL_REFERENCE
                     ? rinfo->target_external_reference()
                     : rinfo->target_address();
  return static_cast<uint32_t>(addr);
#endif
}

struct Header {
  uint32_t total_function_count;
  uint32_t imported_function_count;

  static constexpr size_t kSerializedSize =
      sizeof(total_function_count) + sizeof(imported_function_count);

  void Write(Writer* writer) {
    writer->Write(total_function_count);
    writer->Write(imported_function_count);
  }
  void Read(Reader* reader) {
    total_function_count = reader->Read<uint32_t>();
    imported_function_count = reader->Read<uint32_t>();
  }
};

struct CodeHeader {
  uint32_t code_section_size;
  uint32_t constant_pool_offset;
  uint32_t safepoint_table_offset;
  uint32_t handler_table_offset;
  uint32_t code_size;
  uint32_t reloc_size;
  uint32_t source_positions_size;
  uint32_t protected_instructions_size;
  uint32_t stack_slots;
  WasmCode::Tier tier;

  static constexpr size_t kSerializedSize =
      sizeof(code_section_size) + sizeof(constant_pool_offset) +
      sizeof(safepoint_table_offset) + sizeof(handler_table_offset) +
      sizeof(code_size) + sizeof(reloc_size) + sizeof(source_positions_size) +
      sizeof(protected_instructions_size) + sizeof(stack_slots) + sizeof(tier);

  void Write(Writer* writer) {
    writer->Write(code_section_size);
    writer->Write(constant_pool_offset);
    writer->Write(safepoint_table_offset);
    writer->Write(handler_table_offset);
    writer->Write(code_size);
    writer->Write(reloc_size);
    writer->Write(source_positions_size);
    writer->Write(protected_instructions_size);
    writer->Write(stack_slots);
    writer->Write(tier);
  }

  void Read(Reader* reader) {
    code_section_size = reader->Read<uint32_t>();
    constant_pool_offset = reader->Read<uint32_t>();
    safepoint_table_offset = reader->Read<uint32_t>();
    handler_table_offset = reader->Read<uint32_t>();
    code_size = reader->Read<uint32_t>();
    reloc_size = reader->Read<uint32_t>();
    source_positions_size = reader->Read<uint32_t>();
    protected_instructions_size = reader->Read<uint32_t>();
    stack_slots = reader->Read<uint32_t>();
    tier = reader->Read<WasmCode::Tier>();
  }
};

}  // namespace

class V8_EXPORT_PRIVATE NativeModuleSerializer {
 public:
  NativeModuleSerializer() = delete;
  NativeModuleSerializer(Isolate*, const NativeModule*);

  size_t Measure() const;
  bool Write(Writer* writer);

 private:
  size_t MeasureCopiedStubs() const;
  size_t MeasureCode(const WasmCode*) const;

  void WriteHeader(Writer* writer);
  void WriteCopiedStubs(Writer* writer);
  void WriteCodeHeader(Writer* writer, const WasmCode* code);
  void WriteCode(Writer* writer, const WasmCode* code);

  uint32_t EncodeBuiltinOrStub(Address);

  Isolate* const isolate_;
  const NativeModule* const native_module_;
  bool write_called_;

  // wasm and copied stubs reverse lookup
  std::map<Address, uint32_t> wasm_targets_lookup_;
  // immovable builtins and runtime entries lookup
  std::map<Address, uint32_t> reference_table_lookup_;
  std::map<Address, uint32_t> stub_lookup_;
  std::map<Address, uint32_t> builtin_lookup_;

  DISALLOW_COPY_AND_ASSIGN(NativeModuleSerializer);
};

NativeModuleSerializer::NativeModuleSerializer(Isolate* isolate,
                                               const NativeModule* module)
    : isolate_(isolate), native_module_(module), write_called_(false) {
  DCHECK_NOT_NULL(isolate_);
  DCHECK_NOT_NULL(native_module_);
  // TODO(mtrofin): persist the export wrappers. Ideally, we'd only persist
  // the unique ones, i.e. the cache.
  ExternalReferenceTable* table = isolate_->heap()->external_reference_table();
  for (uint32_t i = 0; i < table->size(); ++i) {
    Address addr = table->address(i);
    reference_table_lookup_.insert(std::make_pair(addr, i));
  }
  // Defer populating stub_lookup_ to when we write the stubs.
  for (auto pair : native_module_->trampolines_) {
    v8::internal::Code* code = Code::GetCodeFromTargetAddress(pair.first);
    int builtin_index = code->builtin_index();
    if (builtin_index >= 0) {
      uint32_t tag = static_cast<uint32_t>(builtin_index);
      builtin_lookup_.insert(std::make_pair(pair.second, tag));
    }
  }
}

size_t NativeModuleSerializer::MeasureCopiedStubs() const {
  size_t size = sizeof(uint32_t);  // number of stubs
  for (auto pair : native_module_->trampolines_) {
    v8::internal::Code* code = Code::GetCodeFromTargetAddress(pair.first);
    int builtin_index = code->builtin_index();
    if (builtin_index < 0) size += sizeof(uint32_t);  // stub key
  }
  return size;
}

size_t NativeModuleSerializer::MeasureCode(const WasmCode* code) const {
  return code->instructions().size() + code->reloc_info().size() +
         code->source_positions().size() +
         code->protected_instructions().size() *
             sizeof(trap_handler::ProtectedInstructionData);
}

size_t NativeModuleSerializer::Measure() const {
  size_t size = Header::kSerializedSize + MeasureCopiedStubs();
  uint32_t first_wasm_fn = native_module_->num_imported_functions();
  uint32_t total_fns = native_module_->function_count();
  for (uint32_t i = first_wasm_fn; i < total_fns; ++i) {
    size += CodeHeader::kSerializedSize;
  }
  for (uint32_t i = first_wasm_fn; i < total_fns; ++i) {
    size += MeasureCode(native_module_->code(i));
  }
  return size;
}

void NativeModuleSerializer::WriteHeader(Writer* writer) {
  Header header;
  header.total_function_count = native_module_->function_count();
  header.imported_function_count = native_module_->num_imported_functions();
  header.Write(writer);
}

void NativeModuleSerializer::WriteCopiedStubs(Writer* writer) {
  // Write the number of stubs and their keys.
  // TODO(all) Serialize the stubs as WasmCode.
  size_t stubs_size = MeasureCopiedStubs();
  // Get the stub count from the number of keys.
  size_t num_stubs = (stubs_size - sizeof(uint32_t)) / sizeof(uint32_t);
  writer->Write(base::checked_cast<uint32_t>(num_stubs));
  uint32_t stub_id = kFirstStubId;

  for (auto pair : native_module_->trampolines_) {
    v8::internal::Code* code = Code::GetCodeFromTargetAddress(pair.first);
    int builtin_index = code->builtin_index();
    if (builtin_index < 0) {
      stub_lookup_.insert(std::make_pair(pair.second, stub_id));
      writer->Write(code->stub_key());
      ++stub_id;
    }
  }
}

void NativeModuleSerializer::WriteCodeHeader(Writer* writer,
                                             const WasmCode* code) {
  CodeHeader header;
  header.code_section_size = base::checked_cast<uint32_t>(MeasureCode(code));
  header.constant_pool_offset =
      base::checked_cast<uint32_t>(code->constant_pool_offset());
  header.safepoint_table_offset =
      base::checked_cast<uint32_t>(code->safepoint_table_offset());
  header.handler_table_offset =
      base::checked_cast<uint32_t>(code->handler_table_offset());
  header.code_size = base::checked_cast<uint32_t>(code->instructions().size());
  header.reloc_size = base::checked_cast<uint32_t>(code->reloc_info().size());
  header.source_positions_size =
      base::checked_cast<uint32_t>(code->source_positions().size());
  header.protected_instructions_size =
      base::checked_cast<uint32_t>(code->protected_instructions().size());
  header.stack_slots = code->stack_slots();
  header.tier = code->tier();
  header.Write(writer);
}

void NativeModuleSerializer::WriteCode(Writer* writer, const WasmCode* code) {
  size_t code_size = code->instructions().size();
  // Get a pointer to the destination buffer, to hold relocated code.
  byte* serialized_code_start = writer->current_buffer().start();
  byte* code_start = serialized_code_start;
  writer->Skip(code_size);
  // Write the reloc info, source positions, and protected code.
  writer->WriteVector(code->reloc_info());
  writer->WriteVector(code->source_positions());
  writer->WriteVector(
      {reinterpret_cast<const byte*>(code->protected_instructions().data()),
       sizeof(trap_handler::ProtectedInstructionData) *
           code->protected_instructions().size()});
#if V8_TARGET_ARCH_MIPS || V8_TARGET_ARCH_MIPS64 || V8_TARGET_ARCH_ARM
  // On platforms that don't support misaligned word stores, copy to an aligned
  // buffer if necessary so we can relocate the serialized code.
  std::unique_ptr<byte[]> aligned_buffer;
  if (!IsAligned(reinterpret_cast<Address>(serialized_code_start),
                 kInt32Size)) {
    aligned_buffer.reset(new byte[code_size]);
    code_start = aligned_buffer.get();
  }
#endif
  memcpy(code_start, code->instructions().start(), code_size);
  // Relocate the code.
  int mask = RelocInfo::ModeMask(RelocInfo::CODE_TARGET) |
             RelocInfo::ModeMask(RelocInfo::WASM_CALL) |
             RelocInfo::ModeMask(RelocInfo::RUNTIME_ENTRY) |
             RelocInfo::ModeMask(RelocInfo::EXTERNAL_REFERENCE);
  RelocIterator orig_iter(code->instructions(), code->reloc_info(),
                          code->constant_pool(), mask);
  for (RelocIterator iter(
           {code_start, code->instructions().size()}, code->reloc_info(),
           reinterpret_cast<Address>(code_start) + code->constant_pool_offset(),
           mask);
       !iter.done(); iter.next(), orig_iter.next()) {
    RelocInfo::Mode mode = orig_iter.rinfo()->rmode();
    switch (mode) {
      case RelocInfo::CODE_TARGET: {
        Address orig_target = orig_iter.rinfo()->target_address();
        uint32_t tag = EncodeBuiltinOrStub(orig_target);
        SetWasmCalleeTag(iter.rinfo(), tag);
      } break;
      case RelocInfo::WASM_CALL: {
        Address orig_target = orig_iter.rinfo()->wasm_call_address();
        uint32_t tag = wasm_targets_lookup_[orig_target];
        SetWasmCalleeTag(iter.rinfo(), tag);
      } break;
      case RelocInfo::RUNTIME_ENTRY: {
        Address orig_target = orig_iter.rinfo()->target_address();
        auto ref_iter = reference_table_lookup_.find(orig_target);
        DCHECK(ref_iter != reference_table_lookup_.end());
        uint32_t tag = ref_iter->second;
        SetWasmCalleeTag(iter.rinfo(), tag);
      } break;
      case RelocInfo::EXTERNAL_REFERENCE: {
        Address orig_target = orig_iter.rinfo()->target_external_reference();
        auto ref_iter = reference_table_lookup_.find(orig_target);
        DCHECK(ref_iter != reference_table_lookup_.end());
        uint32_t tag = ref_iter->second;
        SetWasmCalleeTag(iter.rinfo(), tag);
      } break;
      default:
        UNREACHABLE();
    }
  }
  // If we copied to an aligned buffer, copy code into serialized buffer.
  if (code_start != serialized_code_start) {
    memcpy(serialized_code_start, code_start, code_size);
  }
}

uint32_t NativeModuleSerializer::EncodeBuiltinOrStub(Address address) {
  auto builtin_iter = builtin_lookup_.find(address);
  uint32_t tag = 0;
  if (builtin_iter != builtin_lookup_.end()) {
    uint32_t id = builtin_iter->second;
    DCHECK_LT(id, std::numeric_limits<uint16_t>::max());
    tag = id << 16;
  } else {
    auto stub_iter = stub_lookup_.find(address);
    DCHECK(stub_iter != stub_lookup_.end());
    uint32_t id = stub_iter->second;
    DCHECK_LT(id, std::numeric_limits<uint16_t>::max());
    tag = id & 0x0000FFFF;
  }
  return tag;
}

bool NativeModuleSerializer::Write(Writer* writer) {
  DCHECK(!write_called_);
  write_called_ = true;

  WriteHeader(writer);
  WriteCopiedStubs(writer);

  uint32_t total_fns = native_module_->function_count();
  uint32_t first_wasm_fn = native_module_->num_imported_functions();
  for (uint32_t i = first_wasm_fn; i < total_fns; ++i) {
    WriteCodeHeader(writer, native_module_->code(i));
  }
  for (uint32_t i = first_wasm_fn; i < total_fns; ++i) {
    WriteCode(writer, native_module_->code(i));
  }
  return true;
}

size_t GetSerializedNativeModuleSize(
    Isolate* isolate, Handle<WasmCompiledModule> compiled_module) {
  NativeModule* native_module = compiled_module->GetNativeModule();
  NativeModuleSerializer serializer(isolate, native_module);
  return kVersionSize + serializer.Measure();
}

bool SerializeNativeModule(Isolate* isolate,
                           Handle<WasmCompiledModule> compiled_module,
                           Vector<byte> buffer) {
  NativeModule* native_module = compiled_module->GetNativeModule();
  NativeModuleSerializer serializer(isolate, native_module);
  size_t measured_size = serializer.Measure();
  if (buffer.size() < measured_size) return false;

  Writer writer(buffer);
  WriteVersion(isolate, &writer);

  return serializer.Write(&writer);
}

class V8_EXPORT_PRIVATE NativeModuleDeserializer {
 public:
  NativeModuleDeserializer() = delete;
  NativeModuleDeserializer(Isolate*, NativeModule*);

  bool Read(Reader* reader);

 private:
  bool ReadHeader(Reader* reader);
  CodeHeader ReadCodeHeader(Reader* reader);
  bool ReadCode(Reader* reader, const CodeHeader& header, uint32_t fn_index);
  bool ReadStubs(Reader* reader);
  Address GetTrampolineOrStubFromTag(uint32_t);

  Isolate* const isolate_;
  NativeModule* const native_module_;

  std::vector<Address> stubs_;
  bool read_called_;

  DISALLOW_COPY_AND_ASSIGN(NativeModuleDeserializer);
};

NativeModuleDeserializer::NativeModuleDeserializer(Isolate* isolate,
                                                   NativeModule* native_module)
    : isolate_(isolate), native_module_(native_module), read_called_(false) {}

bool NativeModuleDeserializer::ReadHeader(Reader* reader) {
  Header header;
  header.Read(reader);

  return header.total_function_count == native_module_->function_count() &&
         header.imported_function_count ==
             native_module_->num_imported_functions();
}

bool NativeModuleDeserializer::ReadStubs(Reader* reader) {
  size_t num_stubs = reader->Read<uint32_t>();
  stubs_.reserve(num_stubs);
  for (size_t i = 0; i < num_stubs; ++i) {
    uint32_t key = reader->Read<uint32_t>();
    v8::internal::Code* stub =
        *(v8::internal::CodeStub::GetCode(isolate_, key).ToHandleChecked());
    stubs_.push_back(native_module_->GetLocalAddressFor(handle(stub)));
  }
  return true;
}

CodeHeader NativeModuleDeserializer::ReadCodeHeader(Reader* reader) {
  CodeHeader header;
  header.Read(reader);
  return header;
}

bool NativeModuleDeserializer::ReadCode(Reader* reader,
                                        const CodeHeader& header,
                                        uint32_t fn_index) {
  Vector<const byte> code_buffer = {reader->current_location(),
                                    header.code_size};
  reader->Skip(header.code_size);

  std::unique_ptr<byte[]> reloc_info;
  if (header.reloc_size > 0) {
    reloc_info.reset(new byte[header.reloc_size]);
    reader->ReadVector({reloc_info.get(), header.reloc_size});
  }
  std::unique_ptr<byte[]> source_pos;
  if (header.source_positions_size > 0) {
    source_pos.reset(new byte[header.source_positions_size]);
    reader->ReadVector({source_pos.get(), header.source_positions_size});
  }
  std::unique_ptr<ProtectedInstructions> protected_instructions(
      new ProtectedInstructions(header.protected_instructions_size));
  if (header.protected_instructions_size > 0) {
    size_t size = sizeof(trap_handler::ProtectedInstructionData) *
                  protected_instructions->size();
    Vector<byte> data(reinterpret_cast<byte*>(protected_instructions->data()),
                      size);
    reader->ReadVector(data);
  }
  WasmCode* ret = native_module_->AddOwnedCode(
      code_buffer, std::move(reloc_info), header.reloc_size,
      std::move(source_pos), header.source_positions_size, Just(fn_index),
      WasmCode::kFunction, header.constant_pool_offset, header.stack_slots,
      header.safepoint_table_offset, header.handler_table_offset,
      std::move(protected_instructions), header.tier, WasmCode::kNoFlushICache);
  native_module_->code_table_[fn_index] = ret;

  // now relocate the code
  int mask = RelocInfo::ModeMask(RelocInfo::EMBEDDED_OBJECT) |
             RelocInfo::ModeMask(RelocInfo::CODE_TARGET) |
             RelocInfo::ModeMask(RelocInfo::RUNTIME_ENTRY) |
             RelocInfo::ModeMask(RelocInfo::EXTERNAL_REFERENCE) |
             RelocInfo::ModeMask(RelocInfo::WASM_CODE_TABLE_ENTRY);
  for (RelocIterator iter(ret->instructions(), ret->reloc_info(),
                          ret->constant_pool(), mask);
       !iter.done(); iter.next()) {
    RelocInfo::Mode mode = iter.rinfo()->rmode();
    switch (mode) {
      case RelocInfo::EMBEDDED_OBJECT: {
        // We only expect {undefined}. We check for that when we add code.
        iter.rinfo()->set_target_object(isolate_->heap()->undefined_value(),
                                        SKIP_WRITE_BARRIER, SKIP_ICACHE_FLUSH);
        break;
      }
      case RelocInfo::CODE_TARGET: {
        uint32_t tag = GetWasmCalleeTag(iter.rinfo());
        Address target = GetTrampolineOrStubFromTag(tag);
        iter.rinfo()->set_target_address(target, SKIP_WRITE_BARRIER,
                                         SKIP_ICACHE_FLUSH);
        break;
      }
      case RelocInfo::RUNTIME_ENTRY: {
        uint32_t tag = GetWasmCalleeTag(iter.rinfo());
        Address address =
            isolate_->heap()->external_reference_table()->address(tag);
        iter.rinfo()->set_target_runtime_entry(address, SKIP_WRITE_BARRIER,
                                               SKIP_ICACHE_FLUSH);
        break;
      }
      case RelocInfo::EXTERNAL_REFERENCE: {
        uint32_t tag = GetWasmCalleeTag(iter.rinfo());
        Address address =
            isolate_->heap()->external_reference_table()->address(tag);
        iter.rinfo()->set_target_external_reference(address, SKIP_ICACHE_FLUSH);
        break;
      }
      case RelocInfo::WASM_CODE_TABLE_ENTRY: {
        DCHECK(FLAG_wasm_tier_up);
        DCHECK(ret->is_liftoff());
        WasmCode* const* code_table_entry =
            native_module_->code_table().data() + ret->index();
        iter.rinfo()->set_wasm_code_table_entry(
            reinterpret_cast<Address>(code_table_entry), SKIP_ICACHE_FLUSH);
        break;
      }
      default:
        UNREACHABLE();
    }
  }
  // Flush the i-cache here instead of in AddOwnedCode, to include the changes
  // made while iterating over the RelocInfo above.
  Assembler::FlushICache(ret->instructions().start(),
                         ret->instructions().size());

  return true;
}

Address NativeModuleDeserializer::GetTrampolineOrStubFromTag(uint32_t tag) {
  if ((tag & 0x0000FFFF) == 0) {
    int builtin_id = static_cast<int>(tag >> 16);
    v8::internal::Code* builtin = isolate_->builtins()->builtin(builtin_id);
    return native_module_->GetLocalAddressFor(handle(builtin));
  } else {
    DCHECK_EQ(tag & 0xFFFF0000, 0);
    return stubs_[tag - kFirstStubId];
  }
}

bool NativeModuleDeserializer::Read(Reader* reader) {
  DCHECK(!read_called_);
  read_called_ = true;

  if (!ReadHeader(reader)) return false;
  if (!ReadStubs(reader)) return false;
  uint32_t total_fns = native_module_->function_count();
  uint32_t first_wasm_fn = native_module_->num_imported_functions();
  std::vector<CodeHeader> headers(total_fns - first_wasm_fn);
  for (uint32_t i = first_wasm_fn; i < total_fns; ++i) {
    headers[i - first_wasm_fn] = ReadCodeHeader(reader);
  }
  for (uint32_t i = first_wasm_fn; i < total_fns; ++i) {
    if (!ReadCode(reader, headers[i - first_wasm_fn], i)) return false;
  }
  return reader->current_size() == 0;
}

MaybeHandle<WasmModuleObject> DeserializeNativeModule(
    Isolate* isolate, Vector<const byte> data, Vector<const byte> wire_bytes) {
  if (!IsWasmCodegenAllowed(isolate, isolate->native_context())) {
    return {};
  }
  if (!IsSupportedVersion(isolate, data)) {
    return {};
  }
  ModuleResult decode_result =
      SyncDecodeWasmModule(isolate, wire_bytes.start(), wire_bytes.end(), false,
                           i::wasm::kWasmOrigin);
  if (!decode_result.ok()) return {};
  CHECK_NOT_NULL(decode_result.val);
  Handle<String> module_bytes =
      isolate->factory()
          ->NewStringFromOneByte(
              {wire_bytes.start(), static_cast<size_t>(wire_bytes.length())},
              TENURED)
          .ToHandleChecked();
  DCHECK(module_bytes->IsSeqOneByteString());
  // The {managed_module} will take ownership of the {WasmModule} object,
  // and it will be destroyed when the GC reclaims the wrapper object.
  Handle<Managed<WasmModule>> managed_module =
      Managed<WasmModule>::FromUniquePtr(isolate, std::move(decode_result.val));
  Handle<Script> script = CreateWasmScript(isolate, wire_bytes);
  Handle<WasmSharedModuleData> shared = WasmSharedModuleData::New(
      isolate, managed_module, Handle<SeqOneByteString>::cast(module_bytes),
      script, Handle<ByteArray>::null());
  int export_wrappers_size =
      static_cast<int>(shared->module()->num_exported_functions);
  Handle<FixedArray> export_wrappers = isolate->factory()->NewFixedArray(
      static_cast<int>(export_wrappers_size), TENURED);

  // TODO(eholk): We need to properly preserve the flag whether the trap
  // handler was used or not when serializing.
  UseTrapHandler use_trap_handler =
      trap_handler::IsTrapHandlerEnabled() ? kUseTrapHandler : kNoTrapHandler;
  wasm::ModuleEnv env(shared->module(), use_trap_handler,
                      wasm::RuntimeExceptionSupport::kRuntimeExceptionSupport);
  Handle<WasmCompiledModule> compiled_module =
      WasmCompiledModule::New(isolate, shared->module(), env);
  compiled_module->GetNativeModule()->SetSharedModuleData(shared);
  NativeModuleDeserializer deserializer(isolate,
                                        compiled_module->GetNativeModule());

  Reader reader(data + kVersionSize);
  if (!deserializer.Read(&reader)) return {};

  Handle<WasmModuleObject> module_object =
      WasmModuleObject::New(isolate, compiled_module, export_wrappers, shared);

  // TODO(6792): Wrappers below might be cloned using {Factory::CopyCode}. This
  // requires unlocking the code space here. This should eventually be moved
  // into the allocator.
  CodeSpaceMemoryModificationScope modification_scope(isolate->heap());
  CompileJsToWasmWrappers(isolate, module_object, isolate->counters());

  // There are no instances for this module yet, which means we need to reset
  // the module into a state as if the last instance was collected.
  WasmCompiledModule::Reset(isolate, *compiled_module);

  return module_object;
}

}  // namespace wasm
}  // namespace internal
}  // namespace v8
