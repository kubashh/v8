// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/snapshot/embedded/embedded-file-writer.h"

#include <algorithm>
#include <cinttypes>

#include "src/codegen/source-position-table.h"
#include "src/flags/flags.h"  // For ENABLE_CONTROL_FLOW_INTEGRITY_BOOL
#include "src/objects/code-inl.h"
#include "src/snapshot/embedded/embedded-data-inl.h"

namespace v8 {
namespace internal {

namespace {

int WriteDirectiveOrSeparator(PlatformEmbeddedFileWriterBase* w,
                              int current_line_length,
                              DataDirective directive) {
  int printed_chars;
  if (current_line_length == 0) {
    printed_chars = w->IndentedDataDirective(directive);
    DCHECK_LT(0, printed_chars);
  } else {
    printed_chars = fprintf(w->fp(), ",");
    DCHECK_EQ(1, printed_chars);
  }
  return current_line_length + printed_chars;
}

int WriteLineEndIfNeeded(PlatformEmbeddedFileWriterBase* w,
                         int current_line_length, int write_size) {
  static const int kTextWidth = 100;
  // Check if adding ',0xFF...FF\n"' would force a line wrap. This doesn't use
  // the actual size of the string to be written to determine this so it's
  // more conservative than strictly needed.
  if (current_line_length + strlen(",0x") + write_size * 2 > kTextWidth) {
    fprintf(w->fp(), "\n");
    return 0;
  } else {
    return current_line_length;
  }
}

}  // namespace

void patch_jump_offset(uint8_t* jump, int offset) {
  uint8_t* pre_byte = jump - 1;
  uint8_t pre_inst = *pre_byte;
  if (pre_inst == 0xEB || ((pre_inst & 0xf0) == 0x70)) {
    printf("near jump inst 0x%x in patch jump offset, it's error!\n",
           *pre_byte);
  }
  /*if(*pre_byte == 0xE9){
    printf("jump found in patch jump offset, it meet expection\n");
  }*/
  memcpy(reinterpret_cast<void*>(jump), &offset, sizeof(int));
}

void EmbeddedFileWriter::WriteHotBuiltin(PlatformEmbeddedFileWriterBase* w,
                                         const i::EmbeddedData* blob,
                                         const Builtin builtin) const {
  PrintF("WriteHotBuiltin\n");
  const bool is_default_variant =
      std::strcmp(embedded_variant_, kDefaultEmbeddedVariant) == 0;

  base::EmbeddedVector<char, kTemporaryStringLength> builtin_symbol;
  if (is_default_variant) {
    // Create nicer symbol names for the default mode.
    base::SNPrintF(builtin_symbol, "Builtins_%s_hot",
                   i::Builtins::name(builtin));
  } else {
    base::SNPrintF(builtin_symbol, "%s_Builtins_%s_hot", embedded_variant_,
                   i::Builtins::name(builtin));
  }

  const int hot_size =
      builtin_deffered_offset_->at(static_cast<int32_t>(builtin));

  w->DeclareFunctionBegin(builtin_symbol.begin(), hot_size);
  const int builtin_id = static_cast<int>(builtin);
  const std::vector<uint8_t>& current_positions = source_positions_[builtin_id];
  // The code below interleaves bytes of assembly code for the builtin
  // function with source positions at the appropriate offsets.
  base::Vector<const uint8_t> vpos(current_positions.data(),
                                   current_positions.size());
  v8::internal::SourcePositionTableIterator positions(
      vpos, SourcePositionTableIterator::kExternalOnly);
#ifndef DEBUG
  CHECK(positions.done());  // Release builds must not contain debug infos.
#endif
  // Some builtins (JSConstructStubGeneric) have entry points located in the
  // middle of them, we need to store their addresses since they are part of
  // the list of allowed return addresses in the deoptimizer.
  const std::vector<LabelInfo>& current_labels = label_info_[builtin_id];
  auto label = current_labels.begin();

  const uint8_t* data =
      reinterpret_cast<const uint8_t*>(blob->InstructionStartOf(builtin));
  // CHECK_EQ(blob->LayoutDescription(builtin).instruction_offset,
  // builtin_offset_in_snapshot_->at(builtin_id));
  //  fix jump target in data here

  uint8_t* patched_code = new uint8_t[1000000];
  memset(patched_code, 0xcc, 1000000);
  memcpy(patched_code, data, hot_size);

  /*printf("--------code before patch-------\n");
  printf("0x0000 ");
  for(int32_t i = 0; i < hot_size; i ++){
    printf("%02x ", *(data + i));
    if((i + 1) % 8 == 0){
      printf("\n0x%04x ", (i + 1));
    }
  }
  printf("--------code after patch-------\n");*/

  Jumps jumps = builtin_jumps_->at(static_cast<int32_t>(builtin));
  PrintF(
      "patching code of builtin %s, jumps size is %zu, hot part size is 0x%x\n",
      Builtins::name(builtin), jumps.size(), hot_size);
  for (int i = 0; i < static_cast<int>(jumps.size()); i++) {
    Jump jump = jumps[i];
    if (jump.first < hot_size && jump.second >= hot_size) {
      CHECK_LE(jump.first, hot_size - 4);
      printf("real jump from 0x%x to 0x%x\n", jump.first, jump.second);
      int hot_index = static_cast<int32_t>(builtin);
      int cold_index = hot_index + Builtins::kBuiltinCount;

      // Original: *** | A Hot | A Cold | *** --------------- total offset is
      // zero
      // --------------------------------------------------------------------------
      // Current : *** | A Hot | ... | ??? | A Cold| *** ---- total offset
      // comprise both ... and ???
      // ... means the padding for alignment for 64 bytes
      // ??? means the builtins between hot and cold parts

      // int padding = RoundUp<kCodeAlignment>(hot_size + 1) - hot_size;
      int jump_start_offset =
          builtin_offset_in_snapshot_->at(hot_index) + (jump.first + 4);
      int jump_target_offset =
          builtin_offset_in_snapshot_->at(cold_index) + jump.second - hot_size;

      int jump_offset = jump_target_offset - jump_start_offset;
      printf("new normal start offset = 0x%x + 0x%x\n",
             builtin_offset_in_snapshot_->at(hot_index), jump.first + 4);
      printf("new normal end offset = 0x%x + 0x%x - 0x%x\n",
             builtin_offset_in_snapshot_->at(cold_index), jump.second,
             hot_size);
      printf("new normal offset at 0x%x is 0x%x\n", jump.first, jump_offset);
      patch_jump_offset(patched_code + jump.first, jump_offset);
    }
  }

  if (cross_builtin_table_->count(static_cast<int32_t>(builtin)) != 0) {
    int builtin_id = static_cast<int32_t>(builtin);
    CrossBuiltinJumps cross_jumps = cross_builtin_table_->at(builtin_id);
    for (uint32_t i = 0; i < cross_jumps.size(); i++) {
      CrossBuiltinJump cross_jump = cross_jumps[i];
      int target_builtin_id = cross_jump.second;
      if (cross_jump.first >= hot_size) continue;
      // Builtin target_builtin = static_cast<Builtin>(target_builtin_id);
      int jump_offset_in_caller = cross_jump.first;

      int cross_jump_start_offset =
          builtin_offset_in_snapshot_->at(builtin_id) +
          (jump_offset_in_caller + 4);

      int cross_jump_target_offset =
          builtin_offset_in_snapshot_->at(target_builtin_id);

      int cross_jump_offset =
          cross_jump_target_offset - cross_jump_start_offset;
      printf("new cross offset at 0x%x is 0x%x\n", jump_offset_in_caller,
             cross_jump_offset);
      patch_jump_offset(patched_code + jump_offset_in_caller,
                        cross_jump_offset);
    }
  }

  uint32_t size = RoundUp<kCodeAlignment>(hot_size + 1);
  uint32_t i = 0;
  uint32_t next_source_pos_offset =
      static_cast<uint32_t>(positions.done() ? size : positions.code_offset());
  uint32_t next_label_offset = static_cast<uint32_t>(
      (label == current_labels.end()) ? size : label->offset);

  uint32_t next_offset = 0;
  while (i < size) {
    if (i == next_source_pos_offset) {
      // Write source directive.
      w->SourceInfo(positions.source_position().ExternalFileId(),
                    GetExternallyCompiledFilename(
                        positions.source_position().ExternalFileId()),
                    positions.source_position().ExternalLine());
      positions.Advance();
      next_source_pos_offset = static_cast<uint32_t>(
          positions.done() ? size : positions.code_offset());
      CHECK_GE(next_source_pos_offset, i);
    }
    if (i == next_label_offset) {
      WriteBuiltinLabels(w, label->name);
      label++;
      next_label_offset = static_cast<uint32_t>(
          (label == current_labels.end()) ? size : label->offset);
      CHECK_GE(next_label_offset, i);
    }
    next_offset = std::min(next_source_pos_offset, next_label_offset);
    if (next_label_offset != size) {
      PrintF(
          "label not equal to code size, it should just for a specific builtin "
          "which was not compiled by TurboFan\n");
    }
    if (next_source_pos_offset != size) {
      PrintF(
          "source_pos not equal to code size, it should just for a specific "
          "builtin which was not compiled by TurboFan\n");
    }
    WriteBinaryContentsAsInlineAssembly(w, patched_code + i, next_offset - i,
                                        i);
    i = next_offset;
  }

  w->DeclareFunctionEnd(builtin_symbol.begin());
  delete[] patched_code;
}

void EmbeddedFileWriter::WriteColdBuiltin(PlatformEmbeddedFileWriterBase* w,
                                          const i::EmbeddedData* blob,
                                          const Builtin builtin) const {
  PrintF("WriteColdBuiltin\n");
  const bool is_default_variant =
      std::strcmp(embedded_variant_, kDefaultEmbeddedVariant) == 0;

  base::EmbeddedVector<char, kTemporaryStringLength> builtin_symbol;
  if (is_default_variant) {
    // Create nicer symbol names for the default mode.
    base::SNPrintF(builtin_symbol, "Builtins_%s_cold",
                   i::Builtins::name(builtin));
  } else {
    base::SNPrintF(builtin_symbol, "%s_Builtins_%s_cold", embedded_variant_,
                   i::Builtins::name(builtin));
  }

  const int hot_builtin_id = static_cast<int>(builtin);
  const int cold_builtin_id =
      static_cast<int>(builtin) + Builtins::kBuiltinCount;
  const Builtin cold_builtin = Builtins::FromInt(cold_builtin_id);
  const int hot_size = builtin_deffered_offset_->at(hot_builtin_id);
  const int cold_size = builtin_original_size_->at(hot_builtin_id) - hot_size;

  w->DeclareFunctionBegin(builtin_symbol.begin(), cold_size);
  const uint8_t* data =
      reinterpret_cast<const uint8_t*>(blob->InstructionStartOf(cold_builtin));
  // CHECK_EQ(blob->LayoutDescription(cold_builtin).instruction_offset,
  // builtin_offset_in_snapshot_->at(cold_builtin_id));
  //  fix jump target in data here

  uint8_t* patched_code = new uint8_t[1000000];
  memset(patched_code, 0xcc, 1000000);
  memcpy(patched_code, data, cold_size);
  Jumps jumps = builtin_jumps_->at(hot_builtin_id);
  PrintF(
      "patching code of builtin %s, jumps size is %zu, hot part size is 0x%x\n",
      Builtins::name(builtin), jumps.size(), hot_size);
  for (int i = 0; i < static_cast<int>(jumps.size()); i++) {
    Jump jump = jumps[i];
    if (jump.first >= hot_size && jump.second < hot_size) {
      // CHECK_LE(jump.second, hot_size - 3);
      /*if(jump.second > hot_size - 4){
        printf("builtin: %s\n", Builtins::name(builtin));
        printf("jump.second > hot_size - 4\n");
        printf("jump.second is %x, hot_size is %x\n", jump.second, hot_size);
      }*/
      int offset_in_cold_part = jump.first - hot_size;

      // Original: *** | A Hot | A Cold | *** --------------- total offset is
      // zero
      // --------------------------------------------------------------------------
      // Current : *** | A Hot | ... | ??? | A Cold| *** ---- total offset
      // comprise both ... and ???
      // ... means the padding for alignment for 64 bytes
      // ??? means the builtins between hot and cold parts

      int jump_start_offset = builtin_offset_in_snapshot_->at(cold_builtin_id) +
                              (offset_in_cold_part + 4);
      int jump_target_offset =
          builtin_offset_in_snapshot_->at(hot_builtin_id) + jump.second;

      int jump_offset = jump_target_offset - jump_start_offset;
      printf("new normal offset at 0x%x is 0x%x\n", offset_in_cold_part,
             jump_offset);
      patch_jump_offset(patched_code + offset_in_cold_part, jump_offset);
    }
  }

  if (cross_builtin_table_->count(static_cast<int32_t>(builtin)) != 0) {
    CrossBuiltinJumps cross_jumps = cross_builtin_table_->at(hot_builtin_id);
    for (uint32_t i = 0; i < cross_jumps.size(); i++) {
      CrossBuiltinJump cross_jump = cross_jumps[i];
      int target_builtin_id = cross_jump.second;
      if (cross_jump.first < hot_size) continue;
      // Builtin target_builtin = static_cast<Builtin>(target_builtin_id);
      int jump_offset_in_caller = cross_jump.first - hot_size;

      int cross_jump_start_offset =
          builtin_offset_in_snapshot_->at(cold_builtin_id) +
          (jump_offset_in_caller + 4);

      int cross_jump_target_offset =
          builtin_offset_in_snapshot_->at(target_builtin_id);

      int cross_jump_offset =
          cross_jump_target_offset - cross_jump_start_offset;
      printf("new cross offset at 0x%x is 0x%x\n", jump_offset_in_caller,
             cross_jump_offset);
      patch_jump_offset(patched_code + jump_offset_in_caller,
                        cross_jump_offset);
    }
  }

  uint32_t size = RoundUp<kCodeAlignment>(cold_size + 1);
  // uint32_t size = cold_size + 1;
  WriteBinaryContentsAsInlineAssembly(w, patched_code, size);

  w->DeclareFunctionEnd(builtin_symbol.begin());
  delete[] patched_code;
}

void EmbeddedFileWriter::WriteDataBinary(PlatformEmbeddedFileWriterBase* w,
                                         const i::EmbeddedData* blob) const {
  PrintF("WriteDataBinary\n");
  if (!v8_flags.is_mksnapshot) {
    WriteBinaryContentsAsInlineAssembly(w, blob->data(), blob->data_size());
    return;
  }
  uint8_t* patched_data = new uint8_t[2 * blob->data_size()];
  memset(patched_data, 0, 2 * blob->data_size());
  memcpy(patched_data, blob->data(), blob->data_size());

  const uint8_t* origin_data = blob->data();
  const struct i::EmbeddedData::LayoutDescription* original_layout_descs =
      reinterpret_cast<const struct i::EmbeddedData::LayoutDescription*>(
          origin_data + blob->LayoutDescriptionTableOffset());
  const struct i::EmbeddedData::BuiltinLookupEntry* original_lookup_entries =
      reinterpret_cast<const struct i::EmbeddedData::BuiltinLookupEntry*>(
          origin_data + blob->BuiltinLookupEntryTableOffset());

  const struct i::EmbeddedData::LayoutDescription* patched_layout_descs =
      reinterpret_cast<const struct i::EmbeddedData::LayoutDescription*>(
          patched_data + blob->LayoutDescriptionTableOffset());
  const struct i::EmbeddedData::BuiltinLookupEntry* patched_lookup_entries =
      reinterpret_cast<const struct i::EmbeddedData::BuiltinLookupEntry*>(
          patched_data + blob->BuiltinLookupEntryTableOffset());

  printf("i::builtin_offset_in_snapshot_ size is %zu\n",
         i::builtin_offset_in_snapshot_->size());

  int last_cold_end_offset = 0;
  int last_cold_builtin_id = 0;

  for (Builtin builtin = Builtins::kFirst; builtin <= Builtins::kLast;
       ++builtin) {
    uint32_t builtin_id = static_cast<uint32_t>(builtin);

    printf("it's builtin %s\n", Builtins::name(builtin));
    const struct i::EmbeddedData::LayoutDescription* original_layout_desc =
        original_layout_descs + builtin_id;
    const struct i::EmbeddedData::BuiltinLookupEntry* original_lookup_entry =
        original_lookup_entries + builtin_id;

    struct i::EmbeddedData::LayoutDescription* patched_layout_desc =
        const_cast<struct i::EmbeddedData::LayoutDescription*>(
            patched_layout_descs) +
        builtin_id;
    struct i::EmbeddedData::BuiltinLookupEntry* patched_lookup_entry =
        const_cast<struct i::EmbeddedData::BuiltinLookupEntry*>(
            patched_lookup_entries) +
        builtin_id;

    if (i::builtin_offset_in_snapshot_->count(builtin_id) != 1) {
      printf("error when patching data section!\n");
      abort();
    }

    // printf("layout_desc is %p\n", layout_desc);
    // printf("current offset is 0x%x\n", layout_desc->instruction_offset);
    // printf("offset should modify as 0x%x\n",
    // i::builtin_offset_in_snapshot_->at(builtin_id));
    // std::memcpy(&layout_desc->instruction_offset,
    // &(i::builtin_offset_in_snapshot_->at(builtin_id)), sizeof(int32_t));

    bool builtin_splited = false;
    if (builtin_deffered_offset_->count(builtin_id) != 0) {
      builtin_splited = true;
    }

    int builtin_total_size = original_layout_desc->instruction_length;
    patched_layout_desc->instruction_length =
        builtin_splited ? i::builtin_deffered_offset_->at(builtin_id)
                        : builtin_total_size;
    patched_layout_desc->instruction_offset =
        i::builtin_offset_in_snapshot_->at(builtin_id);
    patched_layout_desc->metadata_offset =
        original_layout_desc->metadata_offset;

    patched_lookup_entry->end_offset =
        patched_layout_desc->instruction_offset +
        RoundUp<kCodeAlignment>(patched_layout_desc->instruction_length + 1);
    patched_lookup_entry->builtin_id = original_lookup_entry->builtin_id;
    if (builtin_splited) {
      /*printf("Patched %s_hot desc in snapshot size is 0x%x, offset is 0x%x\n",
             Builtins::name(builtin), patched_layout_desc->instruction_length,
             patched_layout_desc->instruction_offset);*/
    } else {
      /*printf("Patched %s desc in snapshot size is 0x%x, offset is 0x%x\n",
             Builtins::name(builtin), patched_layout_desc->instruction_length,
             patched_layout_desc->instruction_offset);*/
    }

    if (builtin_splited) {
      int cold_id = builtin_id + Builtins::kBuiltinCount;
      struct i::EmbeddedData::LayoutDescription* patched_cold_layout_desc =
          const_cast<struct i::EmbeddedData::LayoutDescription*>(
              patched_layout_descs) +
          cold_id;
      struct i::EmbeddedData::BuiltinLookupEntry* patched_cold_lookup_entry =
          const_cast<struct i::EmbeddedData::BuiltinLookupEntry*>(
              patched_lookup_entries) +
          cold_id;
      patched_cold_layout_desc->instruction_length =
          builtin_total_size - patched_layout_desc->instruction_length;
      patched_cold_layout_desc->instruction_offset =
          i::builtin_offset_in_snapshot_->at(cold_id);
      patched_cold_layout_desc->metadata_offset =
          patched_layout_desc->metadata_offset;

      patched_cold_lookup_entry->end_offset =
          patched_cold_layout_desc->instruction_offset +
          RoundUp<kCodeAlignment>(patched_cold_layout_desc->instruction_length +
                                  1);
      patched_cold_lookup_entry->builtin_id = cold_id;

      last_cold_end_offset = patched_cold_lookup_entry->end_offset;
      last_cold_builtin_id = patched_cold_lookup_entry->builtin_id;

      printf("%s_cold desc in snapshot size is 0x%x, offset is 0x%x\n",
             Builtins::name(builtin),
             patched_cold_layout_desc->instruction_length,
             patched_cold_layout_desc->instruction_offset);
    } else {
      int dummy_id = builtin_id + Builtins::kBuiltinCount;
      struct i::EmbeddedData::LayoutDescription* patched_dummy_layout_desc =
          const_cast<struct i::EmbeddedData::LayoutDescription*>(
              patched_layout_descs) +
          dummy_id;
      struct i::EmbeddedData::BuiltinLookupEntry* patched_dummy_lookup_entry =
          const_cast<struct i::EmbeddedData::BuiltinLookupEntry*>(
              patched_lookup_entries) +
          dummy_id;
      patched_dummy_layout_desc->instruction_length = 0;
      patched_dummy_layout_desc->instruction_offset = blob->code_size();
      patched_dummy_layout_desc->metadata_offset =
          patched_layout_desc->metadata_offset;

      patched_dummy_lookup_entry->end_offset = 0;
      patched_dummy_lookup_entry->builtin_id = 0;

      // set all info for dummy as 0 now, we will reverse traversal and set them
      // later

      // printf("%s_cold desc in snapshot size is 0x%x, offset is 0x%x\n",
      // Builtins::name(builtin), patched_cold_layout_desc->instruction_length,
      // patched_cold_layout_desc->instruction_offset);
    }
  }

  for (int builtin_id = Builtins::kBuiltinCount - 1; builtin_id >= 0;
       builtin_id--) {
    int dummy_id = builtin_id + Builtins::kBuiltinCount;

    struct i::EmbeddedData::BuiltinLookupEntry* patched_dummy_lookup_entry =
        const_cast<struct i::EmbeddedData::BuiltinLookupEntry*>(
            patched_lookup_entries) +
        dummy_id;

    if (patched_dummy_lookup_entry->end_offset != 0) {
      last_cold_end_offset = patched_dummy_lookup_entry->end_offset;
      last_cold_builtin_id = patched_dummy_lookup_entry->builtin_id;
      continue;
    }
    patched_dummy_lookup_entry->end_offset = last_cold_end_offset;
    patched_dummy_lookup_entry->builtin_id = last_cold_builtin_id;
  }

  printf("pathch data section in snapshot finished!\n\n");

  WriteBinaryContentsAsInlineAssembly(w, patched_data, blob->data_size());
  delete[] patched_data;
}

void EmbeddedFileWriter::WriteBuiltin(PlatformEmbeddedFileWriterBase* w,
                                      const i::EmbeddedData* blob,
                                      const Builtin builtin) const {
  PrintF("WriteBuiltin\n");
  const bool is_default_variant =
      std::strcmp(embedded_variant_, kDefaultEmbeddedVariant) == 0;

  base::EmbeddedVector<char, kTemporaryStringLength> builtin_symbol;
  if (is_default_variant) {
    // Create nicer symbol names for the default mode.
    base::SNPrintF(builtin_symbol, "Builtins_%s", i::Builtins::name(builtin));
  } else {
    base::SNPrintF(builtin_symbol, "%s_Builtins_%s", embedded_variant_,
                   i::Builtins::name(builtin));
  }

  // Labels created here will show up in backtraces. We check in
  // Isolate::SetEmbeddedBlob that the blob layout remains unchanged, i.e.
  // that labels do not insert bytes into the middle of the blob byte
  // stream.
  w->DeclareFunctionBegin(builtin_symbol.begin(),
                          blob->InstructionSizeOf(builtin));
  const int builtin_id = static_cast<int>(builtin);
  const std::vector<uint8_t>& current_positions = source_positions_[builtin_id];
  // The code below interleaves bytes of assembly code for the builtin
  // function with source positions at the appropriate offsets.
  base::Vector<const uint8_t> vpos(current_positions.data(),
                                   current_positions.size());
  v8::internal::SourcePositionTableIterator positions(
      vpos, SourcePositionTableIterator::kExternalOnly);

#ifndef DEBUG
  CHECK(positions.done());  // Release builds must not contain debug infos.
#endif

  // Some builtins (InterpreterPushArgsThenFastConstructFunction,
  // JSConstructStubGeneric) have entry points located in the middle of them, we
  // need to store their addresses since they are part of the list of allowed
  // return addresses in the deoptimizer.
  const std::vector<LabelInfo>& current_labels = label_info_[builtin_id];
  auto label = current_labels.begin();

  const uint8_t* data =
      reinterpret_cast<const uint8_t*>(blob->InstructionStartOf(builtin));
  uint32_t size = blob->PaddedInstructionSizeOf(builtin);
  uint32_t i = 0;
  uint32_t next_source_pos_offset =
      static_cast<uint32_t>(positions.done() ? size : positions.code_offset());
  uint32_t next_label_offset = static_cast<uint32_t>(
      (label == current_labels.end()) ? size : label->offset);
  uint32_t next_offset = 0;
  while (i < size) {
    if (i == next_source_pos_offset) {
      // Write source directive.
      w->SourceInfo(positions.source_position().ExternalFileId(),
                    GetExternallyCompiledFilename(
                        positions.source_position().ExternalFileId()),
                    positions.source_position().ExternalLine());
      positions.Advance();
      next_source_pos_offset = static_cast<uint32_t>(
          positions.done() ? size : positions.code_offset());
      CHECK_GE(next_source_pos_offset, i);
    }
    if (i == next_label_offset) {
      WriteBuiltinLabels(w, label->name);
      label++;
      next_label_offset = static_cast<uint32_t>(
          (label == current_labels.end()) ? size : label->offset);
      CHECK_GE(next_label_offset, i);
    }
    next_offset = std::min(next_source_pos_offset, next_label_offset);
    WriteBinaryContentsAsInlineAssembly(w, data + i, next_offset - i, i);
    i = next_offset;
  }

  w->DeclareFunctionEnd(builtin_symbol.begin());
}

void EmbeddedFileWriter::WriteBuiltinLabels(PlatformEmbeddedFileWriterBase* w,
                                            std::string name) const {
  if (ENABLE_CONTROL_FLOW_INTEGRITY_BOOL) {
    w->DeclareSymbolGlobal(name.c_str());
  }

  w->DeclareLabel(name.c_str());
}

void EmbeddedFileWriter::WriteCodeSection(PlatformEmbeddedFileWriterBase* w,
                                          const i::EmbeddedData* blob) const {
  w->Comment(
      "The embedded blob code section starts here. It contains the builtin");
  w->Comment("instruction streams.");
  w->SectionText();

#if V8_TARGET_ARCH_IA32 || V8_TARGET_ARCH_X64
  // UMA needs an exposed function-type label at the start of the embedded
  // code section.
  static const char* kCodeStartForProfilerSymbolName =
      "v8_code_start_for_profiler_";
  static constexpr int kDummyFunctionLength = 1;
  static constexpr int kDummyFunctionData = 0xcc;
  w->DeclareFunctionBegin(kCodeStartForProfilerSymbolName,
                          kDummyFunctionLength);
  // The label must not be at the same address as the first builtin, insert
  // padding bytes.
  WriteDirectiveOrSeparator(w, 0, kByte);
  w->HexLiteral(kDummyFunctionData);
  w->Newline();
  w->DeclareFunctionEnd(kCodeStartForProfilerSymbolName);
#endif

  w->AlignToCodeAlignment();
  w->DeclareSymbolGlobal(EmbeddedBlobCodeSymbol().c_str());
  w->DeclareLabel(EmbeddedBlobCodeSymbol().c_str());

  static_assert(Builtins::kAllBuiltinsAreIsolateIndependent);
  // We will traversal builtins in embedded snapshot order instead of builtin id
  // order.
  if (!v8_flags.is_mksnapshot) {
    for (ReorderedBuiltinIndex embedded_index = 0;
         embedded_index < Builtins::kBuiltinCount; embedded_index++) {
      // TODO(v8:13938): Update the static_cast later when we introduce
      // reordering builtins. At current stage builtin id equals to i in the
      // loop, if we introduce reordering builtin, we may have to map them in
      // another method.
      Builtin builtin = blob->GetBuiltinId(embedded_index);
      printf("Writing builtin: %s\n", Builtins::name(builtin));
      WriteBuiltin(w, blob, builtin);
      printf("-----------------------------------------------------\n");
    }
  } else {
    for (ReorderedBuiltinIndex embedded_index = 0;
         embedded_index < Builtins::kBuiltinCount; embedded_index++) {
      Builtin builtin = blob->GetBuiltinId(embedded_index);
      int builtin_id = static_cast<int>(builtin);
      // PrintF("Next Builtin: %s\n", Builtins::name(builtin));
      if (builtin_deffered_offset_->count(builtin_id) == 0) {
        printf("Writing builtin: %s\n", Builtins::name(builtin));
        WriteNonDeferredBuiltin(w, blob, builtin);
        printf("-----------------------------------------------------\n");
      } else {
        printf("Writing builtin: %s_hot\n", Builtins::name(builtin));
        WriteHotBuiltin(w, blob, builtin);
        printf("-----------------------------------------------------\n");
      }
    }

    for (ReorderedBuiltinIndex embedded_index = 0;
         embedded_index < Builtins::kBuiltinCount; embedded_index++) {
      Builtin builtin = blob->GetBuiltinId(embedded_index);
      int builtin_id = static_cast<int>(builtin);
      if (builtin_deffered_offset_->count(builtin_id) == 0) continue;
      printf("Writing builtin: %s_cold\n", Builtins::name(builtin));
      WriteColdBuiltin(w, blob, builtin);
      printf("-----------------------------------------------------\n");
    }
  }
  w->AlignToPageSizeIfNeeded();
  w->Newline();
}

void EmbeddedFileWriter::WriteNonDeferredBuiltin(
    PlatformEmbeddedFileWriterBase* w, const i::EmbeddedData* blob,
    const Builtin builtin) const {
  PrintF("WriteNonDeferredBuiltin\n");
  const bool is_default_variant =
      std::strcmp(embedded_variant_, kDefaultEmbeddedVariant) == 0;

  base::EmbeddedVector<char, kTemporaryStringLength> builtin_symbol;
  if (is_default_variant) {
    // Create nicer symbol names for the default mode.
    base::SNPrintF(builtin_symbol, "Builtins_%s", i::Builtins::name(builtin));
  } else {
    base::SNPrintF(builtin_symbol, "%s_Builtins_%s", embedded_variant_,
                   i::Builtins::name(builtin));
  }

  // Labels created here will show up in backtraces. We check in
  // Isolate::SetEmbeddedBlob that the blob layout remains unchanged, i.e.
  // that labels do not insert bytes into the middle of the blob byte
  // stream.
  w->DeclareFunctionBegin(builtin_symbol.begin(),
                          blob->InstructionSizeOf(builtin));
  const int builtin_id = static_cast<int>(builtin);
  const std::vector<uint8_t>& current_positions = source_positions_[builtin_id];
  // The code below interleaves bytes of assembly code for the builtin
  // function with source positions at the appropriate offsets.
  base::Vector<const uint8_t> vpos(current_positions.data(),
                                   current_positions.size());
  v8::internal::SourcePositionTableIterator positions(
      vpos, SourcePositionTableIterator::kExternalOnly);

#ifndef DEBUG
  CHECK(positions.done());  // Release builds must not contain debug infos.
#endif

  // Some builtins (JSConstructStubGeneric) have entry points located in the
  // middle of them, we need to store their addresses since they are part of
  // the list of allowed return addresses in the deoptimizer.
  const std::vector<LabelInfo>& current_labels = label_info_[builtin_id];
  auto label = current_labels.begin();

  const uint8_t* data =
      reinterpret_cast<const uint8_t*>(blob->InstructionStartOf(builtin));
  uint32_t size = blob->PaddedInstructionSizeOf(builtin);

  uint8_t* patched_code = new uint8_t[1000000];
  memset(patched_code, 0xcc, 1000000);
  memcpy(patched_code, data, size);

  if (cross_builtin_table_->count(static_cast<int32_t>(builtin)) != 0) {
    PrintF("patching cross jump of builtin %s, \n", Builtins::name(builtin));
    int builtin_id = static_cast<int32_t>(builtin);
    CrossBuiltinJumps cross_jumps = cross_builtin_table_->at(builtin_id);
    for (uint32_t i = 0; i < cross_jumps.size(); i++) {
      CrossBuiltinJump cross_jump = cross_jumps[i];
      int target_builtin_id = cross_jump.second;
      // Builtin target_builtin = static_cast<Builtin>(target_builtin_id);

      int cross_jump_start_offset =
          builtin_offset_in_snapshot_->at(builtin_id) + (cross_jump.first + 4);

      int cross_jump_target_offset =
          builtin_offset_in_snapshot_->at(target_builtin_id);

      int cross_jump_offset =
          cross_jump_target_offset - cross_jump_start_offset;
      printf("non deferred cross jump from 0x%x to %s\n", cross_jump.first,
             Builtins::name(static_cast<Builtin>(target_builtin_id)));
      printf("caller offset is 0x%x\n",
             builtin_offset_in_snapshot_->at(builtin_id));
      printf("callee offset is 0x%x\n",
             builtin_offset_in_snapshot_->at(target_builtin_id));
      printf("modify jump offset as 0x%x\n", cross_jump_offset);
      patch_jump_offset(patched_code + cross_jump.first, cross_jump_offset);
    }
  }

  uint32_t i = 0;
  uint32_t next_source_pos_offset =
      static_cast<uint32_t>(positions.done() ? size : positions.code_offset());
  uint32_t next_label_offset = static_cast<uint32_t>(
      (label == current_labels.end()) ? size : label->offset);
  uint32_t next_offset = 0;
  while (i < size) {
    if (i == next_source_pos_offset) {
      // Write source directive.
      w->SourceInfo(positions.source_position().ExternalFileId(),
                    GetExternallyCompiledFilename(
                        positions.source_position().ExternalFileId()),
                    positions.source_position().ExternalLine());
      positions.Advance();
      next_source_pos_offset = static_cast<uint32_t>(
          positions.done() ? size : positions.code_offset());
      CHECK_GE(next_source_pos_offset, i);
    }
    if (i == next_label_offset) {
      WriteBuiltinLabels(w, label->name);
      label++;
      next_label_offset = static_cast<uint32_t>(
          (label == current_labels.end()) ? size : label->offset);
      CHECK_GE(next_label_offset, i);
    }
    next_offset = std::min(next_source_pos_offset, next_label_offset);
    // printf("write length 0x%x\n", next_offset - i);
    WriteBinaryContentsAsInlineAssembly(w, patched_code + i, next_offset - i,
                                        i);
    i = next_offset;
  }

  w->DeclareFunctionEnd(builtin_symbol.begin());
}

void EmbeddedFileWriter::WriteFileEpilogue(PlatformEmbeddedFileWriterBase* w,
                                           const i::EmbeddedData* blob) const {
  {
    base::EmbeddedVector<char, kTemporaryStringLength>
        embedded_blob_code_size_symbol;
    base::SNPrintF(embedded_blob_code_size_symbol,
                   "v8_%s_embedded_blob_code_size_", embedded_variant_);

    w->Comment("The size of the embedded blob code in bytes.");
    w->SectionRoData();
    w->AlignToDataAlignment();
    w->DeclareUint32(embedded_blob_code_size_symbol.begin(), blob->code_size());
    w->Newline();

    base::EmbeddedVector<char, kTemporaryStringLength>
        embedded_blob_data_size_symbol;
    base::SNPrintF(embedded_blob_data_size_symbol,
                   "v8_%s_embedded_blob_data_size_", embedded_variant_);

    w->Comment("The size of the embedded blob data section in bytes.");
    w->DeclareUint32(embedded_blob_data_size_symbol.begin(), blob->data_size());
    w->Newline();
  }

#if defined(V8_OS_WIN64)
  {
    base::EmbeddedVector<char, kTemporaryStringLength> unwind_info_symbol;
    base::SNPrintF(unwind_info_symbol, "%s_Builtins_UnwindInfo",
                   embedded_variant_);

    w->MaybeEmitUnwindData(unwind_info_symbol.begin(),
                           EmbeddedBlobCodeSymbol().c_str(), blob,
                           reinterpret_cast<const void*>(&unwind_infos_[0]));
  }
#endif  // V8_OS_WIN64

  w->FileEpilogue();
}

// static
void EmbeddedFileWriter::WriteBinaryContentsAsInlineAssembly(
    PlatformEmbeddedFileWriterBase* w, const uint8_t* data, uint32_t size,
    uint32_t start_offset) {
  // Print content for code in embedded.s
  printf("0x%04x ", start_offset);
  for (uint32_t i = 0; i < size; i++) {
    printf("%02x ", *(data + i));
    if ((i + 1) % 8 == 0) {
      printf("\n0x%04x ", (start_offset + i + 1));
    }
  }
  printf("\n");
  int current_line_length = 0;
  uint32_t i = 0;

  // Begin by writing out byte chunks.
  const DataDirective directive = w->ByteChunkDataDirective();
  const int byte_chunk_size = DataDirectiveSize(directive);
  for (; i + byte_chunk_size < size; i += byte_chunk_size) {
    current_line_length =
        WriteDirectiveOrSeparator(w, current_line_length, directive);
    current_line_length += w->WriteByteChunk(data + i);
    current_line_length =
        WriteLineEndIfNeeded(w, current_line_length, byte_chunk_size);
  }
  if (current_line_length != 0) w->Newline();
  current_line_length = 0;

  // Write any trailing bytes one-by-one.
  for (; i < size; i++) {
    current_line_length =
        WriteDirectiveOrSeparator(w, current_line_length, kByte);
    current_line_length += w->HexLiteral(data[i]);
    current_line_length = WriteLineEndIfNeeded(w, current_line_length, 1);
  }

  if (current_line_length != 0) w->Newline();
}

int EmbeddedFileWriter::LookupOrAddExternallyCompiledFilename(
    const char* filename) {
  auto result = external_filenames_.find(filename);
  if (result != external_filenames_.end()) {
    return result->second;
  }
  int new_id =
      ExternalFilenameIndexToId(static_cast<int>(external_filenames_.size()));
  external_filenames_.insert(std::make_pair(filename, new_id));
  external_filenames_by_index_.push_back(filename);
  DCHECK_EQ(external_filenames_by_index_.size(), external_filenames_.size());
  return new_id;
}

const char* EmbeddedFileWriter::GetExternallyCompiledFilename(
    int fileid) const {
  size_t index = static_cast<size_t>(ExternalFilenameIdToIndex(fileid));
  DCHECK_GE(index, 0);
  DCHECK_LT(index, external_filenames_by_index_.size());

  return external_filenames_by_index_[index];
}

int EmbeddedFileWriter::GetExternallyCompiledFilenameCount() const {
  return static_cast<int>(external_filenames_.size());
}

void EmbeddedFileWriter::PrepareBuiltinSourcePositionMap(Builtins* builtins) {
  for (Builtin builtin = Builtins::kFirst; builtin <= Builtins::kLast;
       ++builtin) {
    // Retrieve the SourcePositionTable and copy it.
    Code code = builtins->code(builtin);
    ByteArray source_position_table = code->source_position_table();
    std::vector<unsigned char> data(
        source_position_table->GetDataStartAddress(),
        source_position_table->GetDataEndAddress());
    source_positions_[static_cast<int>(builtin)] = data;
  }
}

void EmbeddedFileWriter::PrepareBuiltinLabelInfoMap(int create_offset,
                                                    int invoke_offset) {
  label_info_[static_cast<int>(Builtin::kJSConstructStubGeneric)].push_back(
      {create_offset, "construct_stub_create_deopt_addr"});
  label_info_[static_cast<int>(
                  Builtin::kInterpreterPushArgsThenFastConstructFunction)]
      .push_back({invoke_offset, "construct_stub_invoke_deopt_addr"});
}

}  // namespace internal
}  // namespace v8
