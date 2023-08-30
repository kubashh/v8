// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/snapshot/embedded/embedded-data.h"

#include "src/codegen/assembler-inl.h"
#include "src/codegen/callable.h"
#include "src/snapshot/embedded/embedded-data-inl.h"
#include "src/snapshot/snapshot-utils.h"
#include "src/snapshot/sort-builtins.h"

namespace v8 {
namespace internal {

Builtin EmbeddedData::TryLookupCode(Address address) const {
  if (!IsInCodeRange(address)) return Builtin::kNoBuiltinId;

  // Note: Addresses within the padding section between builtins (i.e. within
  // start + size <= address < start + padded_size) are interpreted as belonging
  // to the preceding builtin.
  uint32_t offset =
      static_cast<uint32_t>(address - reinterpret_cast<Address>(RawCode()));

  const struct BuiltinLookupEntry* start =
      BuiltinLookupEntry(static_cast<ReorderedBuiltinIndex>(0));
  const struct BuiltinLookupEntry* end = start + kTableSize;
  const struct BuiltinLookupEntry* desc =
      std::upper_bound(start, end, offset,
                       [](uint32_t o, const struct BuiltinLookupEntry& desc) {
                         return o < desc.end_offset;
                       });
  Builtin builtin = static_cast<Builtin>(desc->builtin_id);
  DCHECK_LT(address,
            InstructionStartOf(builtin) + PaddedInstructionSizeOf(builtin));
  DCHECK_GE(address, InstructionStartOf(builtin));
  return builtin;
}

// static
bool OffHeapInstructionStream::PcIsOffHeap(Isolate* isolate, Address pc) {
  // Mksnapshot calls this while the embedded blob is not available yet.
  if (isolate->embedded_blob_code() == nullptr) return false;
  DCHECK_NOT_NULL(Isolate::CurrentEmbeddedBlobCode());

  if (EmbeddedData::FromBlob(isolate).IsInCodeRange(pc)) return true;
  return isolate->is_short_builtin_calls_enabled() &&
         EmbeddedData::FromBlob().IsInCodeRange(pc);
}

// static
bool OffHeapInstructionStream::TryGetAddressForHashing(
    Isolate* isolate, Address address, uint32_t* hashable_address) {
  // Mksnapshot calls this while the embedded blob is not available yet.
  if (isolate->embedded_blob_code() == nullptr) return false;
  DCHECK_NOT_NULL(Isolate::CurrentEmbeddedBlobCode());

  EmbeddedData d = EmbeddedData::FromBlob(isolate);
  if (d.IsInCodeRange(address)) {
    *hashable_address = d.AddressForHashing(address);
    return true;
  }

  if (isolate->is_short_builtin_calls_enabled()) {
    d = EmbeddedData::FromBlob();
    if (d.IsInCodeRange(address)) {
      *hashable_address = d.AddressForHashing(address);
      return true;
    }
  }
  return false;
}

// static
Builtin OffHeapInstructionStream::TryLookupCode(Isolate* isolate,
                                                Address address) {
  // Mksnapshot calls this while the embedded blob is not available yet.
  if (isolate->embedded_blob_code() == nullptr) return Builtin::kNoBuiltinId;
  DCHECK_NOT_NULL(Isolate::CurrentEmbeddedBlobCode());

  Builtin builtin = EmbeddedData::FromBlob(isolate).TryLookupCode(address);

  if (isolate->is_short_builtin_calls_enabled() &&
      !Builtins::IsBuiltinId(builtin)) {
    builtin = EmbeddedData::FromBlob().TryLookupCode(address);
  }

#ifdef V8_COMPRESS_POINTERS_IN_SHARED_CAGE
  if (V8_SHORT_BUILTIN_CALLS_BOOL && !Builtins::IsBuiltinId(builtin)) {
    // When shared pointer compression cage is enabled and it has the embedded
    // code blob copy then it could have been used regardless of whether the
    // isolate uses it or knows about it or not (see
    // InstructionStream::OffHeapInstructionStart()).
    // So, this blob has to be checked too.
    CodeRange* code_range = CodeRange::GetProcessWideCodeRange();
    if (code_range && code_range->embedded_blob_code_copy() != nullptr) {
      builtin = EmbeddedData::FromBlob(code_range).TryLookupCode(address);
    }
  }
#endif
  return builtin;
}

// static
void OffHeapInstructionStream::CreateOffHeapOffHeapInstructionStream(
    Isolate* isolate, uint8_t** code, uint32_t* code_size, uint8_t** data,
    uint32_t* data_size) {
  // Create the embedded blob from scratch using the current Isolate's heap.
  EmbeddedData::PrepareDataAndCode(isolate);
  EmbeddedData d = EmbeddedData::NewFromIsolateWithPatch(isolate);
  // EmbeddedData d = EmbeddedData::NewFromIsolate(isolate);

  // Allocate the backing store that will contain the embedded blob in this
  // Isolate. The backing store is on the native heap, *not* on V8's garbage-
  // collected heap.
  v8::PageAllocator* page_allocator = v8::internal::GetPlatformPageAllocator();
  const uint32_t alignment =
      static_cast<uint32_t>(page_allocator->AllocatePageSize());

  void* const requested_allocation_code_address =
      AlignedAddress(isolate->heap()->GetRandomMmapAddr(), alignment);
  const uint32_t allocation_code_size = RoundUp(d.code_size(), alignment);
  uint8_t* allocated_code_bytes = static_cast<uint8_t*>(AllocatePages(
      page_allocator, requested_allocation_code_address, allocation_code_size,
      alignment, PageAllocator::kReadWrite));
  CHECK_NOT_NULL(allocated_code_bytes);

  void* const requested_allocation_data_address =
      AlignedAddress(isolate->heap()->GetRandomMmapAddr(), alignment);
  const uint32_t allocation_data_size = RoundUp(d.data_size(), alignment);
  uint8_t* allocated_data_bytes = static_cast<uint8_t*>(AllocatePages(
      page_allocator, requested_allocation_data_address, allocation_data_size,
      alignment, PageAllocator::kReadWrite));
  CHECK_NOT_NULL(allocated_data_bytes);

  // Copy the embedded blob into the newly allocated backing store. Switch
  // permissions to read-execute since builtin code is immutable from now on
  // and must be executable in case any JS execution is triggered.
  //
  // Once this backing store is set as the current_embedded_blob, V8 cannot tell
  // the difference between a 'real' embedded build (where the blob is embedded
  // in the binary) and what we are currently setting up here (where the blob is
  // on the native heap).
  std::memcpy(allocated_code_bytes, d.code(), d.code_size());
  if (v8_flags.experimental_flush_embedded_blob_icache) {
    FlushInstructionCache(allocated_code_bytes, d.code_size());
  }

  CHECK(SetPermissions(page_allocator, allocated_code_bytes,
                       allocation_code_size, PageAllocator::kReadExecute));

  std::memcpy(allocated_data_bytes, d.data(), d.data_size());
  CHECK(SetPermissions(page_allocator, allocated_data_bytes,
                       allocation_data_size, PageAllocator::kRead));

  *code = allocated_code_bytes;
  *code_size = d.code_size();
  *data = allocated_data_bytes;
  *data_size = d.data_size();

  d.Dispose();
}

// static
void OffHeapInstructionStream::FreeOffHeapOffHeapInstructionStream(
    uint8_t* code, uint32_t code_size, uint8_t* data, uint32_t data_size) {
  v8::PageAllocator* page_allocator = v8::internal::GetPlatformPageAllocator();
  const uint32_t page_size =
      static_cast<uint32_t>(page_allocator->AllocatePageSize());
  FreePages(page_allocator, code, RoundUp(code_size, page_size));
  FreePages(page_allocator, data, RoundUp(data_size, page_size));
}

namespace {

void FinalizeEmbeddedCodeTargets(Isolate* isolate, EmbeddedData* blob) {
  static const int kRelocMask =
      RelocInfo::ModeMask(RelocInfo::CODE_TARGET) |
      RelocInfo::ModeMask(RelocInfo::RELATIVE_CODE_TARGET);

  static_assert(Builtins::kAllBuiltinsAreIsolateIndependent);
  for (Builtin builtin = Builtins::kFirst; builtin <= Builtins::kLast;
       ++builtin) {
    Code code = isolate->builtins()->code(builtin);
    RelocIterator on_heap_it(code, kRelocMask);
    RelocIterator off_heap_it(blob, code, kRelocMask);

#if defined(V8_TARGET_ARCH_X64) || defined(V8_TARGET_ARCH_ARM64) ||    \
    defined(V8_TARGET_ARCH_ARM) || defined(V8_TARGET_ARCH_IA32) ||     \
    defined(V8_TARGET_ARCH_S390) || defined(V8_TARGET_ARCH_RISCV64) || \
    defined(V8_TARGET_ARCH_LOONG64) || defined(V8_TARGET_ARCH_RISCV32)
    // On these platforms we emit relative builtin-to-builtin
    // jumps for isolate independent builtins in the snapshot. This fixes up the
    // relative jumps to the right offsets in the snapshot.
    // See also: InstructionStream::IsIsolateIndependent.
    PrintF("finalize cross builtin jump in builtin %s\n",
           Builtins::name(builtin));
    while (!on_heap_it.done()) {
      DCHECK(!off_heap_it.done());

      RelocInfo* rinfo = on_heap_it.rinfo();
      DCHECK_EQ(rinfo->rmode(), off_heap_it.rinfo()->rmode());
      Code target_code = Code::FromTargetAddress(rinfo->target_address());
      CHECK(Builtins::IsIsolateIndependentBuiltin(target_code));

      // Do not emit write-barrier for off-heap writes.
      off_heap_it.rinfo()->set_off_heap_target_address(
          blob->InstructionStartOf(target_code.builtin_id()));
      PrintF("pc is 0x%lx, offset is 0x%lx\n", off_heap_it.rinfo()->pc(),
             on_heap_it.rinfo()->pc() - code.instruction_start());

      on_heap_it.next();
      off_heap_it.next();
    }
    DCHECK(off_heap_it.done());
#else
    // Architectures other than x64 and arm/arm64 do not use pc-relative calls
    // and thus must not contain embedded code targets. Instead, we use an
    // indirection through the root register.
    CHECK(on_heap_it.done());
    CHECK(off_heap_it.done());
#endif
  }
}

void EnsureRelocatable(Code code) {
  if (code->relocation_size() == 0) return;

  // On some architectures (arm) the builtin might have a non-empty reloc
  // info containing a CONST_POOL entry. These entries don't have to be
  // updated when InstructionStream object is relocated, so it's safe to drop
  // the reloc info alltogether. If it wasn't the case then we'd have to store
  // it in the metadata.
  for (RelocIterator it(code); !it.done(); it.next()) {
    CHECK_EQ(it.rinfo()->rmode(), RelocInfo::CONST_POOL);
  }
}

}  // namespace

// static
EmbeddedData EmbeddedData::NewFromIsolate(Isolate* isolate) {
  Builtins* builtins = isolate->builtins();

  // Store instruction stream lengths and offsets.
  std::vector<struct LayoutDescription> layout_descriptions(kTableSize);
  std::vector<struct BuiltinLookupEntry> offset_descriptions(kTableSize);

  bool saw_unsafe_builtin = false;
  uint32_t raw_code_size = 0;
  uint32_t raw_data_size = 0;
  static_assert(Builtins::kAllBuiltinsAreIsolateIndependent);

  std::vector<Builtin> reordered_builtins;
  if (v8_flags.reorder_builtins &&
      BuiltinsCallGraph::Get()->all_hash_matched()) {
    DCHECK(v8_flags.turbo_profiling_input.value());
    // TODO(ishell, v8:13938): avoid the binary size overhead for non-mksnapshot
    // binaries.
    BuiltinsSorter sorter;
    std::vector<uint32_t> builtin_sizes;
    for (Builtin i = Builtins::kFirst; i <= Builtins::kLast; ++i) {
      Code code = builtins->code(i);
      uint32_t instruction_size =
          static_cast<uint32_t>(code->instruction_size());
      uint32_t padding_size = PadAndAlignCode(instruction_size);
      builtin_sizes.push_back(padding_size);
    }
    reordered_builtins = sorter.SortBuiltins(
        v8_flags.turbo_profiling_input.value(), builtin_sizes);
    CHECK_EQ(reordered_builtins.size(), Builtins::kBuiltinCount);
  }

  for (ReorderedBuiltinIndex embedded_index = 0;
       embedded_index < Builtins::kBuiltinCount; embedded_index++) {
    Builtin builtin;
    if (reordered_builtins.empty()) {
      builtin = static_cast<Builtin>(embedded_index);
    } else {
      builtin = reordered_builtins[embedded_index];
    }
    Code code = builtins->code(builtin);

    // Sanity-check that the given builtin is isolate-independent.
    if (!code->IsIsolateIndependent(isolate)) {
      saw_unsafe_builtin = true;
      fprintf(stderr, "%s is not isolate-independent.\n",
              Builtins::name(builtin));
    }

    uint32_t instruction_size = static_cast<uint32_t>(code->instruction_size());
    DCHECK_EQ(0, raw_code_size % kCodeAlignment);
    {
      // We use builtin id as index in layout_descriptions.
      const int builtin_id = static_cast<int>(builtin);
      struct LayoutDescription& layout_desc = layout_descriptions[builtin_id];
      layout_desc.instruction_offset = raw_code_size;
      layout_desc.instruction_length = instruction_size;
      layout_desc.metadata_offset = raw_data_size;
    }
    // Align the start of each section.
    raw_code_size += PadAndAlignCode(instruction_size);
    raw_data_size += PadAndAlignData(code->metadata_size());

    {
      // We use embedded index as index in offset_descriptions.
      struct BuiltinLookupEntry& offset_desc =
          offset_descriptions[embedded_index];
      offset_desc.end_offset = raw_code_size;
      offset_desc.builtin_id = static_cast<uint32_t>(builtin);
    }
  }
  CHECK_WITH_MSG(
      !saw_unsafe_builtin,
      "One or more builtins marked as isolate-independent either contains "
      "isolate-dependent code or aliases the off-heap trampoline register. "
      "If in doubt, ask jgruber@");

  // Allocate space for the code section, value-initialized to 0.
  static_assert(RawCodeOffset() == 0);
  const uint32_t blob_code_size = RawCodeOffset() + raw_code_size;
  uint8_t* const blob_code = new uint8_t[blob_code_size]();

  // Allocate space for the data section, value-initialized to 0.
  static_assert(
      IsAligned(FixedDataSize(), InstructionStream::kMetadataAlignment));
  const uint32_t blob_data_size = FixedDataSize() + raw_data_size;
  uint8_t* const blob_data = new uint8_t[blob_data_size]();

  // Initially zap the entire blob, effectively padding the alignment area
  // between two builtins with int3's (on x64/ia32).
  ZapCode(reinterpret_cast<Address>(blob_code), blob_code_size);

  // Hash relevant parts of the Isolate's heap and store the result.
  {
    static_assert(IsolateHashSize() == kSizetSize);
    const size_t hash = isolate->HashIsolateForEmbeddedBlob();
    std::memcpy(blob_data + IsolateHashOffset(), &hash, IsolateHashSize());
  }

  // Write the layout_descriptions tables.
  DCHECK_EQ(LayoutDescriptionTableSize(),
            sizeof(layout_descriptions[0]) * layout_descriptions.size());
  std::memcpy(blob_data + LayoutDescriptionTableOffset(),
              layout_descriptions.data(), LayoutDescriptionTableSize());

  // Write the builtin_offset_descriptions tables.
  DCHECK_EQ(BuiltinLookupEntryTableSize(),
            sizeof(offset_descriptions[0]) * offset_descriptions.size());
  std::memcpy(blob_data + BuiltinLookupEntryTableOffset(),
              offset_descriptions.data(), BuiltinLookupEntryTableSize());

  // .. and the variable-size data section.
  uint8_t* const raw_metadata_start = blob_data + RawMetadataOffset();
  static_assert(Builtins::kAllBuiltinsAreIsolateIndependent);
  for (Builtin builtin = Builtins::kFirst; builtin <= Builtins::kLast;
       ++builtin) {
    Code code = builtins->code(builtin);
    uint32_t offset =
        layout_descriptions[static_cast<int>(builtin)].metadata_offset;
    uint8_t* dst = raw_metadata_start + offset;
    DCHECK_LE(RawMetadataOffset() + offset + code->metadata_size(),
              blob_data_size);
    std::memcpy(dst, reinterpret_cast<uint8_t*>(code->metadata_start()),
                code->metadata_size());
  }
  CHECK_IMPLIES(
      kMaxPCRelativeCodeRangeInMB,
      static_cast<size_t>(raw_code_size) <= kMaxPCRelativeCodeRangeInMB * MB);

  // .. and the variable-size code section.
  uint8_t* const raw_code_start = blob_code + RawCodeOffset();
  static_assert(Builtins::kAllBuiltinsAreIsolateIndependent);
  for (Builtin builtin = Builtins::kFirst; builtin <= Builtins::kLast;
       ++builtin) {
    Code code = builtins->code(builtin);
    uint32_t offset =
        layout_descriptions[static_cast<int>(builtin)].instruction_offset;
    uint8_t* dst = raw_code_start + offset;
    DCHECK_LE(RawCodeOffset() + offset + code->instruction_size(),
              blob_code_size);
    std::memcpy(dst, reinterpret_cast<uint8_t*>(code->instruction_start()),
                code->instruction_size());
  }

  EmbeddedData d(blob_code, blob_code_size, blob_data, blob_data_size);

  // Fix up call targets that point to other embedded builtins.
  FinalizeEmbeddedCodeTargets(isolate, &d);

  // Hash the blob and store the result.
  {
    static_assert(EmbeddedBlobDataHashSize() == kSizetSize);
    const size_t data_hash = d.CreateEmbeddedBlobDataHash();
    std::memcpy(blob_data + EmbeddedBlobDataHashOffset(), &data_hash,
                EmbeddedBlobDataHashSize());

    static_assert(EmbeddedBlobCodeHashSize() == kSizetSize);
    const size_t code_hash = d.CreateEmbeddedBlobCodeHash();
    std::memcpy(blob_data + EmbeddedBlobCodeHashOffset(), &code_hash,
                EmbeddedBlobCodeHashSize());

    DCHECK_EQ(data_hash, d.CreateEmbeddedBlobDataHash());
    DCHECK_EQ(data_hash, d.EmbeddedBlobDataHash());
    DCHECK_EQ(code_hash, d.CreateEmbeddedBlobCodeHash());
    DCHECK_EQ(code_hash, d.EmbeddedBlobCodeHash());
  }

  if (DEBUG_BOOL) {
    for (Builtin builtin = Builtins::kFirst; builtin <= Builtins::kLast;
         ++builtin) {
      Code code = builtins->code(builtin);
      CHECK_EQ(d.InstructionSizeOf(builtin), code->instruction_size());
    }
  }

  // Ensure that InterpreterEntryTrampolineForProfiling is relocatable.
  // See v8_flags.interpreted_frames_native_stack for details.
  EnsureRelocatable(
      builtins->code(Builtin::kInterpreterEntryTrampolineForProfiling));

  if (v8_flags.serialization_statistics) d.PrintStatistics();

  return d;
}

// static
EmbeddedData EmbeddedData::NewFromIsolateWithPatch(Isolate* isolate) {
  // Patch data and code here, needs to modify hash inside the embedded data
  // We should also patch the Code object in isolate->builtin_table()
  Builtins* builtins = isolate->builtins();
  // Address* builtins_code_object_pointers = isolate->builtin_table();

  // Store instruction stream lengths and offsets.
  std::vector<struct LayoutDescription> layout_descriptions(kTableSize);
  std::vector<struct BuiltinLookupEntry> offset_descriptions(kTableSize);

  bool saw_unsafe_builtin = false;
  uint32_t raw_code_size = 0;
  uint32_t raw_data_size = 0;
  static_assert(Builtins::kAllBuiltinsAreIsolateIndependent);

  std::vector<Builtin> reordered_builtins;
  if (v8_flags.reorder_builtins &&
      BuiltinsCallGraph::Get()->all_hash_matched()) {
    DCHECK(v8_flags.turbo_profiling_input.value());
    // TODO(ishell, v8:13938): avoid the binary size overhead for non-mksnapshot
    // binaries.
    BuiltinsSorter sorter;
    std::vector<uint32_t> builtin_sizes;
    for (Builtin i = Builtins::kFirst; i <= Builtins::kLast; ++i) {
      Code code = builtins->code(i);
      uint32_t instruction_size =
          static_cast<uint32_t>(code->instruction_size());
      uint32_t padding_size = PadAndAlignCode(instruction_size);
      builtin_sizes.push_back(padding_size);
    }
    reordered_builtins = sorter.SortBuiltins(
        v8_flags.turbo_profiling_input.value(), builtin_sizes);
    CHECK_EQ(reordered_builtins.size(), Builtins::kBuiltinCount);
    for (uint32_t i = 0; i < reordered_builtins.size(); i++) {
      PrintF("the %d th builtin is %s\n", i,
             Builtins::name(reordered_builtins.at(i)));
    }
  }

  // We will traversal builtins in embedded snapshot order instead of builtin id
  // order.
  // Hot first
  for (ReorderedBuiltinIndex embedded_index = 0;
       embedded_index < Builtins::kBuiltinCount; embedded_index++) {
    // TODO(v8:13938): Update the static_cast later when we introduce reordering
    // builtins. At current stage builtin id equals to i in the loop, if we
    // introduce reordering builtin, we may have to map them in another method.
    Builtin builtin;
    if (reordered_builtins.empty()) {
      builtin = static_cast<Builtin>(embedded_index);
    } else {
      builtin = reordered_builtins[embedded_index];
    }
    int hot_id = static_cast<int>(builtin);
    Code hot_code = builtins->code(builtin);

    // Sanity-check that the given builtin is isolate-independent.
    if (!hot_code.IsIsolateIndependent(isolate)) {
      saw_unsafe_builtin = true;
      fprintf(stderr, "%s is not isolate-independent.\n",
              Builtins::name(builtin));
    }

    if (builtin_deffered_offset_->count(hot_id) != 0) {
      hot_code.set_instruction_size(builtin_deffered_offset_->at(hot_id));
    }

    uint32_t instruction_size =
        static_cast<uint32_t>(hot_code.instruction_size());
    DCHECK_EQ(0, raw_code_size % kCodeAlignment);
    {
      // We use builtin id as index in layout_descriptions.
      const int builtin_id = static_cast<int>(builtin);
      struct LayoutDescription& layout_desc = layout_descriptions[builtin_id];
      layout_desc.instruction_offset = raw_code_size;
      layout_desc.instruction_length = instruction_size;
      layout_desc.metadata_offset = raw_data_size;
    }
    // Align the start of each section.
    raw_code_size += PadAndAlignCode(instruction_size);
    raw_data_size += PadAndAlignData(hot_code.metadata_size());

    {
      // We use embedded index as index in offset_descriptions.
      struct BuiltinLookupEntry& offset_desc =
          offset_descriptions[embedded_index];
      offset_desc.end_offset = raw_code_size;
      offset_desc.builtin_id = static_cast<uint32_t>(builtin);
    }
  }

  // For cold parts, it includes the real cold part of deferred blocks, and
  // dummy cold part with empty inst stream

  int last_cold_end_offset = 0;
  int last_cold_builtin_id = 0;

  for (ReorderedBuiltinIndex embedded_index = 0;
       embedded_index < Builtins::kBuiltinCount; embedded_index++) {
    Builtin hot_builtin;
    if (reordered_builtins.empty()) {
      hot_builtin = static_cast<Builtin>(embedded_index);
    } else {
      hot_builtin = reordered_builtins[embedded_index];
    }
    int hot_id = static_cast<int>(hot_builtin);
    Code hot_code = builtins->code(hot_builtin);

    bool is_splitted = false;
    if (builtin_deffered_offset_->count(hot_id) != 0) {
      is_splitted = true;
    }

    Builtin cold_builtin =
        static_cast<Builtin>(hot_id + Builtins::kBuiltinCount);
    const int cold_builtin_id = static_cast<int>(cold_builtin);
    const int cold_embeded_index =
        static_cast<int>(embedded_index + Builtins::kBuiltinCount);

    uint32_t instruction_size = static_cast<uint32_t>(
        builtin_original_size_->at(hot_id) - hot_code.instruction_size());
    DCHECK_EQ(0, raw_code_size % kCodeAlignment);
    if (is_splitted) {
      // We use builtin id as index in layout_descriptions.
      struct LayoutDescription& layout_desc =
          layout_descriptions[cold_builtin_id];
      layout_desc.instruction_offset = raw_code_size;
      layout_desc.instruction_length = instruction_size;
      layout_desc.metadata_offset = layout_descriptions[hot_id].metadata_offset;
    } else {
      struct LayoutDescription& layout_desc =
          layout_descriptions[cold_builtin_id];
      layout_desc.instruction_offset = 0xffffffff;
      layout_desc.instruction_length = 0;
      layout_desc.metadata_offset = layout_descriptions[hot_id].metadata_offset;

      struct i::EmbeddedData::BuiltinLookupEntry& dummy_lookup_entry =
          offset_descriptions[cold_embeded_index];

      dummy_lookup_entry.end_offset = 0xffffffff;
      dummy_lookup_entry.builtin_id = 0xffffffff;
    }
    // Align the start of each section.
    if (instruction_size > 0)
      raw_code_size += PadAndAlignCode(instruction_size);

    if (is_splitted) {
      struct BuiltinLookupEntry& offset_desc =
          offset_descriptions[cold_embeded_index];
      offset_desc.end_offset = raw_code_size;
      offset_desc.builtin_id = static_cast<uint32_t>(cold_builtin_id);

      last_cold_builtin_id = cold_builtin_id;
      last_cold_end_offset = raw_code_size;
    }
  }

  for (int builtin_id = Builtins::kBuiltinCount - 1; builtin_id >= 0;
       builtin_id--) {
    // Builtin builtin = Builtins::FromInt(builtin_id);
    int dummy_id = builtin_id + Builtins::kBuiltinCount;

    // struct i::EmbeddedData::LayoutDescription& dummy_layout_desc =
    //     layout_descriptions[dummy_id];
    struct i::EmbeddedData::BuiltinLookupEntry& dummy_lookup_entry =
        offset_descriptions[dummy_id];

    if (dummy_lookup_entry.end_offset != 0xffffffff) {
      last_cold_end_offset = dummy_lookup_entry.end_offset;
      last_cold_builtin_id = dummy_lookup_entry.builtin_id;
      continue;
    }
    dummy_lookup_entry.end_offset = last_cold_end_offset;
    dummy_lookup_entry.builtin_id = last_cold_builtin_id;

    /*printf("%s_cold desc in snapshot size is 0x%x, offset is 0x%x\n",
           Builtins::name(builtin), dummy_layout_desc.instruction_length,
           dummy_lookup_entry.end_offset);*/
  }

  CHECK_WITH_MSG(
      !saw_unsafe_builtin,
      "One or more builtins marked as isolate-independent either contains "
      "isolate-dependent code or aliases the off-heap trampoline register. "
      "If in doubt, ask jgruber@");

  // Allocate space for the code section, value-initialized to 0.
  static_assert(RawCodeOffset() == 0);
  const uint32_t blob_code_size = RawCodeOffset() + raw_code_size;
  uint8_t* const blob_code = new uint8_t[blob_code_size]();

  // Allocate space for the data section, value-initialized to 0.
  static_assert(
      IsAligned(FixedDataSize(), InstructionStream::kMetadataAlignment));
  const uint32_t blob_data_size = FixedDataSize() + raw_data_size;
  uint8_t* const blob_data = new uint8_t[blob_data_size]();

  // Initially zap the entire blob, effectively padding the alignment area
  // between two builtins with int3's (on x64/ia32).
  ZapCode(reinterpret_cast<Address>(blob_code), blob_code_size);

  // Hash relevant parts of the Isolate's heap and store the result.
  {
    static_assert(IsolateHashSize() == kSizetSize);
    const size_t hash = isolate->HashIsolateForEmbeddedBlob();
    std::memcpy(blob_data + IsolateHashOffset(), &hash, IsolateHashSize());
  }

  // Write the layout_descriptions tables.
  DCHECK_EQ(LayoutDescriptionTableSize(),
            sizeof(layout_descriptions[0]) * layout_descriptions.size());
  std::memcpy(blob_data + LayoutDescriptionTableOffset(),
              layout_descriptions.data(), LayoutDescriptionTableSize());

  // Write the builtin_offset_descriptions tables.
  DCHECK_EQ(BuiltinLookupEntryTableSize(),
            sizeof(offset_descriptions[0]) * offset_descriptions.size());
  std::memcpy(blob_data + BuiltinLookupEntryTableOffset(),
              offset_descriptions.data(), BuiltinLookupEntryTableSize());

  // .. and the variable-size data section.
  uint8_t* const raw_metadata_start = blob_data + RawMetadataOffset();
  static_assert(Builtins::kAllBuiltinsAreIsolateIndependent);
  for (Builtin builtin = Builtins::kFirst; builtin <= Builtins::kLast;
       ++builtin) {
    Code code = builtins->code(builtin);
    uint32_t offset =
        layout_descriptions[static_cast<int>(builtin)].metadata_offset;
    uint8_t* dst = raw_metadata_start + offset;
    DCHECK_LE(RawMetadataOffset() + offset + code.metadata_size(),
              blob_data_size);
    std::memcpy(dst,
                reinterpret_cast<uint8_t*>(
                    code.instruction_start() +
                    builtin_original_size_->at(static_cast<int>(builtin))),
                code.metadata_size());
  }
  CHECK_IMPLIES(
      kMaxPCRelativeCodeRangeInMB,
      static_cast<size_t>(raw_code_size) <= kMaxPCRelativeCodeRangeInMB * MB);

  // .. and the variable-size code section.
  uint8_t* const raw_code_start = blob_code + RawCodeOffset();
  static_assert(Builtins::kAllBuiltinsAreIsolateIndependent);

  // Copy heap code(hot part) into offheap snapshot
  for (Builtin builtin = Builtins::kFirst; builtin <= Builtins::kLast;
       ++builtin) {
    Code code = builtins->code(builtin);
    uint32_t offset =
        layout_descriptions[static_cast<int>(builtin)].instruction_offset;
    uint8_t* dst = raw_code_start + offset;
    PrintF("builtin: %s, offset is 0x%x, in snapshot is 0x%x\n",
           Builtins::name(builtin), offset,
           builtin_offset_in_snapshot_->at(static_cast<int>(builtin)));
    CHECK_EQ(offset,
             builtin_offset_in_snapshot_->at(static_cast<int>(builtin)));
    DCHECK_LE(RawCodeOffset() + offset + code.instruction_size(),
              blob_code_size);

    // Print the copy process from on heap code to off heap embedded.s
    // print end

    std::memcpy(dst, reinterpret_cast<uint8_t*>(code.instruction_start()),
                code.instruction_size());
    /*printf("Copying hot part\n");
    printf("0x0000 ");
    for(int32_t i = 0; i < code.instruction_size(); i ++){
      printf("%02x ", *(dst + i));
      if((i + 1) % 8 == 0){
        printf("\n0x%04x ", (i + 1));
      }
    }*/
  }

  // Copy heap code(cold part) into offheap snapshot
  for (Builtin builtin = Builtins::kFirst; builtin <= Builtins::kLast;
       ++builtin) {
    // We don't have cold code object here, it's hot cold object
    if (builtin_deffered_offset_->count(static_cast<int>(builtin)) == 0)
      continue;
    Code code = builtins->code(builtin);
    int cold_id = static_cast<int>(builtin) + Builtins::kBuiltinCount;
    uint32_t cold_offset = layout_descriptions[cold_id].instruction_offset;
    uint32_t cold_size = layout_descriptions[cold_id].instruction_length;
    int deferred_offset =
        builtin_deffered_offset_->at(static_cast<int>(builtin));
    if (cold_offset == 0xffffffff) {
      // For dummy, we skip to copy
      continue;
    }
    CHECK_EQ(cold_offset, builtin_offset_in_snapshot_->at(cold_id));
    uint8_t* dst = raw_code_start + cold_offset;

    std::memcpy(
        dst,
        reinterpret_cast<uint8_t*>(code.instruction_start()) + deferred_offset,
        cold_size);
    printf("Copying cold part for builtin %s\n", Builtins::name(builtin));
    printf("0x0000 ");
    for (uint32_t i = 0; i < cold_size; i++) {
      printf("%02x ", *(dst + i));
      if ((i + 1) % 8 == 0) {
        printf("\n0x%04x ", (i + 1));
      }
    }
    printf("\n");
  }

  EmbeddedData d(blob_code, blob_code_size, blob_data, blob_data_size);

  // Fix up call targets that point to other embedded builtins.
  // FinalizeEmbeddedCodeTargets(isolate, &d);

  // Hash the blob and store the result.
  {
    static_assert(EmbeddedBlobDataHashSize() == kSizetSize);
    const size_t data_hash = d.CreateEmbeddedBlobDataHash();
    std::memcpy(blob_data + EmbeddedBlobDataHashOffset(), &data_hash,
                EmbeddedBlobDataHashSize());

    static_assert(EmbeddedBlobCodeHashSize() == kSizetSize);
    const size_t code_hash = d.CreateEmbeddedBlobCodeHash();
    std::memcpy(blob_data + EmbeddedBlobCodeHashOffset(), &code_hash,
                EmbeddedBlobCodeHashSize());

    DCHECK_EQ(data_hash, d.CreateEmbeddedBlobDataHash());
    DCHECK_EQ(data_hash, d.EmbeddedBlobDataHash());
    DCHECK_EQ(code_hash, d.CreateEmbeddedBlobCodeHash());
    DCHECK_EQ(code_hash, d.EmbeddedBlobCodeHash());
  }

  if (DEBUG_BOOL) {
    for (Builtin builtin = Builtins::kFirst; builtin <= Builtins::kLast;
         ++builtin) {
      Code code = builtins->code(builtin);
      CHECK_EQ(d.InstructionSizeOf(builtin), code.instruction_size());
    }
  }

  // Ensure that InterpreterEntryTrampolineForProfiling is relocatable.
  // See v8_flags.interpreted_frames_native_stack for details.
  EnsureRelocatable(
      builtins->code(Builtin::kInterpreterEntryTrampolineForProfiling));

  if (v8_flags.serialization_statistics) d.PrintStatistics();

  return d;
}

// static
void EmbeddedData::PrepareDataAndCode(Isolate* isolate) {
  // Filter jumps with cross hot/cold part, because if both jump inst and jump
  // target are in same part, we don't need to modify code(ip-related offset).
  for (Builtin builtin_ix = Builtins::kFirst; builtin_ix <= Builtins::kLast;
       ++builtin_ix) {
    if (builtin_jumps_->count(static_cast<int32_t>(builtin_ix)) == 0) continue;
    if (builtin_deffered_offset_->count(static_cast<int32_t>(builtin_ix)) !=
        0) {
      printf("deffered offset for %s is 0x%x\n", Builtins::name(builtin_ix),
             builtin_deffered_offset_->at(static_cast<int32_t>(builtin_ix)));
    } else {
      // printf("non deffered offset for %s\n", Builtins::name(builtin_ix));
      continue;
    }
    printf("Jumps for %s:\n", Builtins::name(builtin_ix));
    int deffered_offset =
        builtin_deffered_offset_->at(static_cast<int32_t>(builtin_ix));
    Jumps jumps = builtin_jumps_->at(static_cast<int32_t>(builtin_ix));
    Jumps fliter_jumps = Jumps();
    for (uint32_t i = 0; i < jumps.size(); i++) {
      Jump jump = jumps[i];
      if (jump.first < deffered_offset && jump.second >= deffered_offset) {
        fliter_jumps.push_back(std::make_pair(jump.first, jump.second));
        printf("forward jump from 0x%x to 0x%x.\n", jump.first, jump.second);
      }
      if (jump.first >= deffered_offset && jump.second < deffered_offset) {
        fliter_jumps.push_back(std::make_pair(jump.first, jump.second));
        printf("backward jump from 0x%x to 0x%x.\n", jump.first, jump.second);
      }
    }
    builtin_jumps_->erase(static_cast<int32_t>(builtin_ix));
    builtin_jumps_->emplace(static_cast<int32_t>(builtin_ix), fliter_jumps);
  }

  for (Builtin builtin = Builtins::kFirst; builtin <= Builtins::kLast;
       ++builtin) {
    int builtin_id = static_cast<int32_t>(builtin);
    if (cross_builtin_table_->count(builtin_id) == 0) continue;
    CrossBuiltinJumps cross_jumps = cross_builtin_table_->at(builtin_id);
    printf("builtin %s has cross builtin jumps:\n", Builtins::name(builtin));
    for (uint32_t i = 0; i < cross_jumps.size(); i++) {
      CrossBuiltinJump cross_jump = cross_jumps[i];
      int offset = cross_jump.first;
      // int target = cross_jump.second;
      // Builtin target_builtin = static_cast<Builtin>(target);
      // printf("cross jump from 0x%x to builtin %s\n", offset,
      // Builtins::name(target_builtin));
      printf("cross jump at 0x%x\n", offset);
    }
  }

  std::vector<Builtin> reordered_builtins;
  if (v8_flags.reorder_builtins &&
      BuiltinsCallGraph::Get()->all_hash_matched()) {
    DCHECK(v8_flags.turbo_profiling_input.value());
    // TODO(ishell, v8:13938): avoid the binary size overhead for non-mksnapshot
    // binaries.
    BuiltinsSorter sorter;
    std::vector<uint32_t> builtin_sizes;
    for (Builtin i = Builtins::kFirst; i <= Builtins::kLast; ++i) {
      Code code = isolate->builtins()->code(i);
      uint32_t instruction_size =
          static_cast<uint32_t>(code->instruction_size());
      uint32_t padding_size = PadAndAlignCode(instruction_size);
      builtin_sizes.push_back(padding_size);
    }
    reordered_builtins = sorter.SortBuiltins(
        v8_flags.turbo_profiling_input.value(), builtin_sizes);
    CHECK_EQ(reordered_builtins.size(), Builtins::kBuiltinCount);
  }

  // perpare for offset info
  // hot part
  int32_t snapshot_offset = 0;
  for (int32_t embeded_index = 0; embeded_index < Builtins::kBuiltinCount;
       embeded_index++) {
    Builtin builtin;
    if (reordered_builtins.empty()) {
      builtin = static_cast<Builtin>(embeded_index);
    } else {
      builtin = reordered_builtins[embeded_index];
    }
    int builtin_id = static_cast<int>(builtin);
    Code original_code = isolate->builtins()->code(builtin);
    int original_size = original_code.instruction_size();
    // PrintF("builtin_offset_in_snapshot_ is %p\n",
    // builtin_offset_in_snapshot_); PrintF("builtin_offset_in_snapshot_ size is
    // %zu\n", builtin_offset_in_snapshot_->size());
    builtin_offset_in_snapshot_->emplace(builtin_id, snapshot_offset);
    PrintF("Original size of %s is 0x%x\n", Builtins::name(builtin),
           original_size);

    builtin_original_size_->emplace(builtin_id, original_size);
    int padded_size = 0;
    if (builtin_deffered_offset_->count(builtin_id) == 0) {
      padded_size = RoundUp<kCodeAlignment>(original_size + 1);
    } else {
      int hot_size = builtin_deffered_offset_->at(builtin_id);
      padded_size = RoundUp<kCodeAlignment>(hot_size + 1);
    }
    snapshot_offset += padded_size;
  }
  printf("hot builtin offset is %zu\n", builtin_offset_in_snapshot_->size());
  printf("hot builtin defferred offset size is %zu\n",
         builtin_deffered_offset_->size());

  // Cold part
  for (int32_t embeded_index = 0; embeded_index < Builtins::kBuiltinCount;
       embeded_index++) {
    Builtin hot_builtin;
    if (reordered_builtins.empty()) {
      hot_builtin = static_cast<Builtin>(embeded_index);
    } else {
      hot_builtin = reordered_builtins[embeded_index];
    }
    int hot_id = static_cast<int>(hot_builtin);
    Code original_code = isolate->builtins()->code(hot_builtin);
    int original_size = original_code.instruction_size();

    int32_t cold_id = hot_id + Builtins::kBuiltinCount;
    if (builtin_deffered_offset_->count(hot_id) == 0) continue;
    int builtin_deferred_offset = builtin_deffered_offset_->at(hot_id);
    int cold_size = original_size - builtin_deferred_offset;
    printf(
        "%s original size is 0x%x, deferred offset is 0x%x, cold size is "
        "0x%x\n",
        Builtins::name(hot_builtin), original_size, builtin_deferred_offset,
        cold_size);
    int padded_size = RoundUp<kCodeAlignment>(cold_size + 1);
    builtin_offset_in_snapshot_->emplace(cold_id, snapshot_offset);
    snapshot_offset += padded_size;
  }
  printf("hot + cold builtin offset size is %zu\n",
         builtin_offset_in_snapshot_->size());
  printf("hot builtin defferred offset size is %zu\n",
         builtin_deffered_offset_->size());

  // Log offset in snapshot for hot part
  for (int32_t hot_index = 0; hot_index < Builtins::kBuiltinCount;
       hot_index++) {
    printf("builtin %s", Builtins::name(static_cast<Builtin>(hot_index)));
    if (builtin_deffered_offset_->count(hot_index) != 0) {
      printf("_hot");
    }
    printf(" snapshot offset: 0x%x\n",
           builtin_offset_in_snapshot_->at(hot_index));
  }

  // Log offset in snapshot for cold part
  for (int32_t hot_index = 0; hot_index < Builtins::kBuiltinCount;
       hot_index++) {
    if (builtin_deffered_offset_->count(hot_index) == 0) continue;
    printf("builtin %s_cold", Builtins::name(static_cast<Builtin>(hot_index)));
    printf(
        " snapshot offset: 0x%x\n",
        builtin_offset_in_snapshot_->at(hot_index + Builtins::kBuiltinCount));
  }

  printf("builtin offset size is %zu\n", builtin_offset_in_snapshot_->size());
  printf("builtin defferred offset size is %zu\n",
         builtin_deffered_offset_->size());
  printf("builtin count is %d\n", Builtins::kBuiltinCount);

  static const int kRelocMask =
      RelocInfo::ModeMask(RelocInfo::CODE_TARGET) |
      RelocInfo::ModeMask(RelocInfo::RELATIVE_CODE_TARGET);

  static_assert(Builtins::kAllBuiltinsAreIsolateIndependent);

  for (Builtin builtin = Builtins::kFirst; builtin <= Builtins::kLast;
       ++builtin) {
    Code code = isolate->builtins()->code(builtin);
    RelocIterator on_heap_it(code, kRelocMask);

#if defined(V8_TARGET_ARCH_X64) || defined(V8_TARGET_ARCH_ARM64) ||    \
    defined(V8_TARGET_ARCH_ARM) || defined(V8_TARGET_ARCH_IA32) ||     \
    defined(V8_TARGET_ARCH_S390) || defined(V8_TARGET_ARCH_RISCV64) || \
    defined(V8_TARGET_ARCH_LOONG64) || defined(V8_TARGET_ARCH_RISCV32)
    // On these platforms we emit relative builtin-to-builtin
    // jumps for isolate independent builtins in the snapshot. This fixes up the
    // relative jumps to the right offsets in the snapshot.
    // See also: InstructionStream::IsIsolateIndependent.
    while (!on_heap_it.done()) {
      RelocInfo* rinfo = on_heap_it.rinfo();
      Code target_code = Code::FromTargetAddress(rinfo->target_address());
      CHECK(Builtins::IsIsolateIndependentBuiltin(target_code));

      int caller_builtin_id = static_cast<int>(builtin);
      int callee_builtin_id = static_cast<int>(target_code.builtin_id());
      int jump_offset =
          static_cast<int>(on_heap_it.rinfo()->pc() - code.instruction_start());
      if (cross_builtin_table_->count(caller_builtin_id) == 0) {
        cross_builtin_table_->emplace(caller_builtin_id, CrossBuiltinJumps());
      }
      cross_builtin_table_->at(caller_builtin_id)
          .push_back(std::make_pair(jump_offset, callee_builtin_id));
      // printf("%s calls %s at 0x%x\n", Builtins::name(builtin),
      // Builtins::name(target_code.builtin_id()), jump_offset);

      on_heap_it.next();
    }
#else
    // Architectures other than x64 and arm/arm64 do not use pc-relative calls
    // and thus must not contain embedded code targets. Instead, we use an
    // indirection through the root register.
    CHECK(on_heap_it.done());
#endif
  }
  // perpare ending
}

size_t EmbeddedData::CreateEmbeddedBlobDataHash() const {
  static_assert(EmbeddedBlobDataHashOffset() == 0);
  static_assert(EmbeddedBlobCodeHashOffset() == EmbeddedBlobDataHashSize());
  static_assert(IsolateHashOffset() ==
                EmbeddedBlobCodeHashOffset() + EmbeddedBlobCodeHashSize());
  static constexpr uint32_t kFirstHashedDataOffset = IsolateHashOffset();
  // Hash the entire data section except the embedded blob hash fields
  // themselves.
  base::Vector<const uint8_t> payload(data_ + kFirstHashedDataOffset,
                                      data_size_ - kFirstHashedDataOffset);
  return Checksum(payload);
}

size_t EmbeddedData::CreateEmbeddedBlobCodeHash() const {
  CHECK(v8_flags.text_is_readable);
  base::Vector<const uint8_t> payload(code_, code_size_);
  return Checksum(payload);
}

Builtin EmbeddedData::GetBuiltinId(ReorderedBuiltinIndex embedded_index) const {
  Builtin builtin =
      Builtins::FromInt(BuiltinLookupEntry(embedded_index)->builtin_id);
  return builtin;
}

void EmbeddedData::PrintStatistics() const {
  DCHECK(v8_flags.serialization_statistics);

  constexpr int kCount = Builtins::kBuiltinCount;
  int sizes[kCount];
  static_assert(Builtins::kAllBuiltinsAreIsolateIndependent);
  for (int i = 0; i < kCount; i++) {
    sizes[i] = InstructionSizeOf(Builtins::FromInt(i));
  }

  // Sort for percentiles.
  std::sort(&sizes[0], &sizes[kCount]);

  const int k50th = kCount * 0.5;
  const int k75th = kCount * 0.75;
  const int k90th = kCount * 0.90;
  const int k99th = kCount * 0.99;

  PrintF("EmbeddedData:\n");
  PrintF("  Total size:                  %d\n",
         static_cast<int>(code_size() + data_size()));
  PrintF("  Data size:                   %d\n", static_cast<int>(data_size()));
  PrintF("  Code size:                   %d\n", static_cast<int>(code_size()));
  PrintF("  Instruction size (50th percentile): %d\n", sizes[k50th]);
  PrintF("  Instruction size (75th percentile): %d\n", sizes[k75th]);
  PrintF("  Instruction size (90th percentile): %d\n", sizes[k90th]);
  PrintF("  Instruction size (99th percentile): %d\n", sizes[k99th]);
  PrintF("\n");
}

}  // namespace internal
}  // namespace v8
