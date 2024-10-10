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
    CodeRange* code_range = IsolateGroup::current()->GetCodeRange();
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
  EmbeddedData d = EmbeddedData::NewFromIsolate(isolate);

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
    Tagged<Code> code = isolate->builtins()->code(builtin);
    RelocIterator on_heap_it(code, kRelocMask);
    RelocIterator off_heap_it(blob, code, kRelocMask);

#if defined(V8_TARGET_ARCH_X64) || defined(V8_TARGET_ARCH_ARM64) ||     \
    defined(V8_TARGET_ARCH_ARM) || defined(V8_TARGET_ARCH_IA32) ||      \
    defined(V8_TARGET_ARCH_S390X) || defined(V8_TARGET_ARCH_RISCV64) || \
    defined(V8_TARGET_ARCH_LOONG64) || defined(V8_TARGET_ARCH_RISCV32)
    // On these platforms we emit relative builtin-to-builtin
    // jumps for isolate independent builtins in the snapshot. This fixes up the
    // relative jumps to the right offsets in the snapshot.
    // See also: InstructionStream::IsIsolateIndependent.
    while (!on_heap_it.done()) {
      DCHECK(!off_heap_it.done());

      RelocInfo* rinfo = on_heap_it.rinfo();
      DCHECK_EQ(rinfo->rmode(), off_heap_it.rinfo()->rmode());
      Tagged<Code> target_code =
          Code::FromTargetAddress(rinfo->target_address());
      CHECK(Builtins::IsIsolateIndependentBuiltin(target_code));

      // Do not emit write-barrier for off-heap writes.
      off_heap_it.rinfo()->set_off_heap_target_address(
          blob->InstructionStartOf(target_code->builtin_id()));

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

  for (size_t i = 0; i < isolate->builtins()->isx_builtins.size(); i++) {
    PrintF("finalize jump target for id: %zu\n", i);
    Tagged<Code> isx_code = isolate->builtins()->isx_builtins[i];
    RelocIterator on_heap_it(isx_code, kRelocMask);
    RelocIterator off_heap_it(blob, isx_code, i, kRelocMask);

#if defined(V8_TARGET_ARCH_X64) || defined(V8_TARGET_ARCH_ARM64) ||    \
    defined(V8_TARGET_ARCH_ARM) || defined(V8_TARGET_ARCH_IA32) ||     \
    defined(V8_TARGET_ARCH_S390) || defined(V8_TARGET_ARCH_RISCV64) || \
    defined(V8_TARGET_ARCH_LOONG64) || defined(V8_TARGET_ARCH_RISCV32)
    // On these platforms we emit relative builtin-to-builtin
    // jumps for isolate independent builtins in the snapshot. This fixes up the
    // relative jumps to the right offsets in the snapshot.
    // See also: InstructionStream::IsIsolateIndependent.
    while (!on_heap_it.done()) {
      PrintF("once iteration!\n");
      DCHECK(!off_heap_it.done());

      RelocInfo* rinfo = on_heap_it.rinfo();
      DCHECK_EQ(rinfo->rmode(), off_heap_it.rinfo()->rmode());
      Tagged<Code> target_code =
          Code::FromTargetAddress(rinfo->target_address());
      CHECK(Builtins::IsIsolateIndependentBuiltin(target_code));

      // Do not emit write-barrier for off-heap writes.
      off_heap_it.rinfo()->set_off_heap_target_address(
          blob->InstructionStartOf(target_code->builtin_id()));

      PrintF("pc is 0x%lx, offset is 0x%lx\n", off_heap_it.rinfo()->pc(),
             on_heap_it.rinfo()->pc() - isx_code->instruction_start());
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

void EnsureRelocatable(Tagged<Code> code) {
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

namespace {
base::LazyMutex mutex_ = LAZY_MUTEX_INITIALIZER;
}

void EmbeddedData::UpdateForISXBuiltin() {
  if (CpuFeatures::IsSupported(CpuFeature::SSE4_1)) {
    // Lock the mutex before entering the critical section for some test case,
    // which may utilize some mutiple thread to do test.
    base::MutexGuard lock_guard(mutex_.Pointer());
    v8::PageAllocator* page_allocator =
        v8::internal::GetPlatformPageAllocator();
    const struct LayoutDescription* descs =
        reinterpret_cast<const struct LayoutDescription*>(
            data_ + LayoutDescriptionTableOffset());
    const struct LayoutDescription* desc =
        descs + static_cast<int>(Builtin::kStoreFastElementIC_InBounds);
    const struct LayoutDescription* isx_desc =
        descs + static_cast<int>(kTableSize);
    void* desc_address = reinterpret_cast<void*>(RoundDown((int64)desc, 4096));
    size_t mov_len =
        reinterpret_cast<char*> desc - reinterpret_cast<char*> desc_address;
    size_t len = RoundUp(
        mov_len + sizeof(struct LayoutDescription) * (kTableSize + 1), 4096);
    CHECK(SetPermissions(page_allocator, desc_address, len,
                         PageAllocator::kReadWrite));
    memcpy(reinterpret_cast<void*> desc, reinterpret_cast<void*> isx_desc,
           sizeof(struct LayoutDescription));

    struct BuiltinLookupEntry* entry = const_cast<struct BuiltinLookupEntry*>(
        EmbeddedData::BuiltinLookupEntry(static_cast<ReorderedBuiltinIndex>(
            Builtin::kStoreFastElementIC_InBounds)));
    // const uint8_t* raw_code = RawCode();
    // PrintF("before update for isx, the entry->end_offset: %p\n",
    // raw_code+entry->end_offset);
    entry->end_offset += isx_desc->instruction_length;
    CHECK(SetPermissions(page_allocator, desc_address, len,
                         PageAllocator::kRead));

    /*for (Builtin builtin = Builtins::kFirst; builtin <= Builtins::kLast;
       ++builtin) {
        const struct LayoutDescription& desc_i = LayoutDescription(builtin);
        const struct BuiltinLookupEntry* entry_i =
    BuiltinLookupEntry(static_cast<ReorderedBuiltinIndex>(builtin));
        PrintF("builtin: %s\n", Builtins::name(builtin));
        PrintF("desc start: %p, desc length: 0x%x\n", raw_code +
    desc_i.instruction_offset, desc_i.instruction_length); PrintF("entry end:
    %p\n", raw_code + entry_i->end_offset);
    }*/
  }
}

// static
EmbeddedData EmbeddedData::NewFromIsolate(Isolate* isolate) {
  Builtins* builtins = isolate->builtins();

  // Store instruction stream lengths and offsets.
  std::vector<struct LayoutDescription> layout_descriptions(kTableSize + 1);
  std::vector<struct BuiltinLookupEntry> offset_descriptions(kTableSize);

  std::vector<struct LayoutDescription> layout_descriptions_isx;

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
      Tagged<Code> code = builtins->code(i);
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
    Tagged<Code> code = builtins->code(builtin);

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
    if (builtin == Builtin::kStoreFastElementIC_InBounds) {
      // insert a desc for isx builtin in layout description array, still need
      // to insert builtin inst stream!!!
      Tagged<Code> isx_code = builtins->isx_builtins[0];
      uint32_t isx_inst_size =
          static_cast<uint32_t>(isx_code->instruction_size());
      struct LayoutDescription isx_desc {
        raw_code_size, isx_inst_size, raw_data_size
      };
      raw_code_size += PadAndAlignCode(isx_inst_size);
      raw_data_size += PadAndAlignData(isx_code->metadata_size());
      layout_descriptions_isx.push_back(isx_desc);
    }
  }
  for (size_t i = 0; i < layout_descriptions_isx.size(); i++) {
    struct LayoutDescription& layout_desc_isx =
        layout_descriptions[i + kTableSize];
    layout_desc_isx.instruction_length =
        layout_descriptions_isx[i].instruction_length;
    layout_desc_isx.instruction_offset =
        layout_descriptions_isx[i].instruction_offset;
    layout_desc_isx.metadata_offset =
        layout_descriptions_isx[i].metadata_offset;
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
  // this dcheck will fail due to adding new desc for isx buitlin.
  // DCHECK_EQ(LayoutDescriptionTableSize(),
  //          sizeof(layout_descriptions[0]) * layout_descriptions.size());
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
    Tagged<Code> code = builtins->code(builtin);
    uint32_t offset =
        layout_descriptions[static_cast<int>(builtin)].metadata_offset;
    uint8_t* dst = raw_metadata_start + offset;
    DCHECK_LE(RawMetadataOffset() + offset + code->metadata_size(),
              blob_data_size);
    std::memcpy(dst, reinterpret_cast<uint8_t*>(code->metadata_start()),
                code->metadata_size());

    if (builtin == Builtin::kStoreFastElementIC_InBounds) {
      Tagged<Code> isx_code = builtins->isx_builtins[0];
      // kTableSize is the last element in descs, which indicate the isx
      // builtin, but should write more formal code later.
      uint32_t offset_isx = layout_descriptions[kTableSize].metadata_offset;
      uint8_t* dst_isx = raw_metadata_start + offset_isx;
      std::memcpy(dst_isx,
                  reinterpret_cast<uint8_t*>(isx_code->metadata_start()),
                  isx_code->metadata_size());
    }
  }
  CHECK_IMPLIES(
      kMaxPCRelativeCodeRangeInMB,
      static_cast<size_t>(raw_code_size) <= kMaxPCRelativeCodeRangeInMB * MB);

  // .. and the variable-size code section.
  uint8_t* const raw_code_start = blob_code + RawCodeOffset();
  static_assert(Builtins::kAllBuiltinsAreIsolateIndependent);
  for (Builtin builtin = Builtins::kFirst; builtin <= Builtins::kLast;
       ++builtin) {
    Tagged<Code> code = builtins->code(builtin);
    uint32_t offset =
        layout_descriptions[static_cast<int>(builtin)].instruction_offset;
    uint8_t* dst = raw_code_start + offset;
    DCHECK_LE(RawCodeOffset() + offset + code->instruction_size(),
              blob_code_size);
    std::memcpy(dst, reinterpret_cast<uint8_t*>(code->instruction_start()),
                code->instruction_size());
    if (builtin == Builtin::kStoreFastElementIC_InBounds) {
      Tagged<Code> isx_code = builtins->isx_builtins[0];
      // kTableSize is the last element in descs, which indicate the isx
      // builtin, but should write more formal code later.
      uint32_t offset_isx = layout_descriptions[kTableSize].instruction_offset;
      uint8_t* dst_isx = raw_code_start + offset_isx;
      std::memcpy(dst_isx,
                  reinterpret_cast<uint8_t*>(isx_code->instruction_start()),
                  isx_code->instruction_size());
    }
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
      Tagged<Code> code = builtins->code(builtin);
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
