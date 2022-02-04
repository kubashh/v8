// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/wasm-module-builder.h"

#include "src/base/memory.h"
#include "src/codegen/signature.h"
#include "src/handles/handles.h"
#include "src/init/v8.h"
#include "src/objects/objects-inl.h"
#include "src/wasm/function-body-decoder.h"
#include "src/wasm/leb-helper.h"
#include "src/wasm/wasm-constants.h"
#include "src/wasm/wasm-module.h"
#include "src/zone/zone-containers.h"

namespace v8 {
namespace internal {
namespace wasm {

namespace {

// Emit a section code and the size as a padded varint that can be patched
// later.
size_t EmitSection(SectionCode code, ZoneBuffer* buffer) {
  // Emit the section code.
  buffer->write_u8(code);

  // Emit a placeholder for the length.
  return buffer->reserve_u32v();
}

// Patch the size of a section after it's finished.
void FixupSection(ZoneBuffer* buffer, size_t start) {
  buffer->patch_u32v(start, static_cast<uint32_t>(buffer->offset() - start -
                                                  kPaddedVarInt32Size));
}

}  // namespace

WasmFunctionBuilder::WasmFunctionBuilder(WasmModuleBuilder* builder)
    : builder_(builder),
      locals_(builder->zone()),
      signature_index_(0),
      func_index_(static_cast<uint32_t>(builder->functions.size())),
      body_(builder->zone(), 256),
      i32_temps_(builder->zone()),
      i64_temps_(builder->zone()),
      f32_temps_(builder->zone()),
      f64_temps_(builder->zone()),
      direct_calls_(builder->zone()),
      asm_offsets_(builder->zone(), 8) {}

void WasmFunctionBuilder::EmitByte(byte val) { body_.write_u8(val); }

void WasmFunctionBuilder::EmitI32V(int32_t val) { body_.write_i32v(val); }

void WasmFunctionBuilder::EmitU32V(uint32_t val) { body_.write_u32v(val); }

void WasmFunctionBuilder::SetSignature(const FunctionSig* sig) {
  DCHECK(!locals_.has_sig());
  locals_.set_sig(sig);
  signature_index_ = builder_->add_signature(sig, kNoSuperType);
}

void WasmFunctionBuilder::SetSignature(uint32_t sig_index) {
  DCHECK(!locals_.has_sig());
  DCHECK_EQ(builder_->types[sig_index].kind, TypeDefinition::kFunction);
  signature_index_ = sig_index;
  locals_.set_sig(builder_->types[sig_index].function_sig);
}

uint32_t WasmFunctionBuilder::AddLocal(ValueType type) {
  DCHECK(locals_.has_sig());
  return locals_.AddLocals(1, type);
}

void WasmFunctionBuilder::EmitGetLocal(uint32_t local_index) {
  EmitWithU32V(kExprLocalGet, local_index);
}

void WasmFunctionBuilder::EmitSetLocal(uint32_t local_index) {
  EmitWithU32V(kExprLocalSet, local_index);
}

void WasmFunctionBuilder::EmitTeeLocal(uint32_t local_index) {
  EmitWithU32V(kExprLocalTee, local_index);
}

void WasmFunctionBuilder::EmitCode(const byte* code, uint32_t code_size) {
  body_.write(code, code_size);
}

void WasmFunctionBuilder::Emit(WasmOpcode opcode) { body_.write_u8(opcode); }

void WasmFunctionBuilder::EmitWithPrefix(WasmOpcode opcode) {
  DCHECK_NE(0, opcode & 0xff00);
  body_.write_u8(opcode >> 8);
  if ((opcode >> 8) == WasmOpcode::kSimdPrefix) {
    // SIMD opcodes are LEB encoded
    body_.write_u32v(opcode & 0xff);
  } else {
    body_.write_u8(opcode);
  }
}

void WasmFunctionBuilder::EmitWithU8(WasmOpcode opcode, const byte immediate) {
  body_.write_u8(opcode);
  body_.write_u8(immediate);
}

void WasmFunctionBuilder::EmitWithU8U8(WasmOpcode opcode, const byte imm1,
                                       const byte imm2) {
  body_.write_u8(opcode);
  body_.write_u8(imm1);
  body_.write_u8(imm2);
}

void WasmFunctionBuilder::EmitWithI32V(WasmOpcode opcode, int32_t immediate) {
  body_.write_u8(opcode);
  body_.write_i32v(immediate);
}

void WasmFunctionBuilder::EmitWithU32V(WasmOpcode opcode, uint32_t immediate) {
  body_.write_u8(opcode);
  body_.write_u32v(immediate);
}

namespace {
void WriteValueType(ZoneBuffer* buffer, const ValueType& type) {
  buffer->write_u8(type.value_type_code());
  if (type.encoding_needs_heap_type()) {
    buffer->write_i32v(type.heap_type().code());
  }
  if (type.is_rtt()) {
    buffer->write_u32v(type.ref_index());
  }
}
}  // namespace

void WasmFunctionBuilder::EmitValueType(ValueType type) {
  WriteValueType(&body_, type);
}

void WasmFunctionBuilder::EmitI32Const(int32_t value) {
  EmitWithI32V(kExprI32Const, value);
}

void WasmFunctionBuilder::EmitI64Const(int64_t value) {
  body_.write_u8(kExprI64Const);
  body_.write_i64v(value);
}

void WasmFunctionBuilder::EmitF32Const(float value) {
  body_.write_u8(kExprF32Const);
  body_.write_f32(value);
}

void WasmFunctionBuilder::EmitF64Const(double value) {
  body_.write_u8(kExprF64Const);
  body_.write_f64(value);
}

void WasmFunctionBuilder::EmitDirectCallIndex(uint32_t index) {
  DirectCallIndex call;
  call.offset = body_.size();
  call.direct_index = index;
  direct_calls_.push_back(call);
  byte placeholder_bytes[kMaxVarInt32Size] = {0};
  EmitCode(placeholder_bytes, arraysize(placeholder_bytes));
}

void WasmFunctionBuilder::SetName(base::Vector<const char> name) {
  name_ = name;
}

void WasmFunctionBuilder::AddAsmWasmOffset(size_t call_position,
                                           size_t to_number_position) {
  // We only want to emit one mapping per byte offset.
  DCHECK(asm_offsets_.size() == 0 || body_.size() > last_asm_byte_offset_);

  DCHECK_LE(body_.size(), kMaxUInt32);
  uint32_t byte_offset = static_cast<uint32_t>(body_.size());
  asm_offsets_.write_u32v(byte_offset - last_asm_byte_offset_);
  last_asm_byte_offset_ = byte_offset;

  DCHECK_GE(std::numeric_limits<uint32_t>::max(), call_position);
  uint32_t call_position_u32 = static_cast<uint32_t>(call_position);
  asm_offsets_.write_i32v(call_position_u32 - last_asm_source_position_);

  DCHECK_GE(std::numeric_limits<uint32_t>::max(), to_number_position);
  uint32_t to_number_position_u32 = static_cast<uint32_t>(to_number_position);
  asm_offsets_.write_i32v(to_number_position_u32 - call_position_u32);
  last_asm_source_position_ = to_number_position_u32;
}

void WasmFunctionBuilder::SetAsmFunctionStartPosition(
    size_t function_position) {
  DCHECK_EQ(0, asm_func_start_source_position_);
  DCHECK_GE(std::numeric_limits<uint32_t>::max(), function_position);
  uint32_t function_position_u32 = static_cast<uint32_t>(function_position);
  // Must be called before emitting any asm.js source position.
  DCHECK_EQ(0, asm_offsets_.size());
  asm_func_start_source_position_ = function_position_u32;
  last_asm_source_position_ = function_position_u32;
}

void WasmFunctionBuilder::SetCompilationHint(
    WasmCompilationHintStrategy strategy, WasmCompilationHintTier baseline,
    WasmCompilationHintTier top_tier) {
  uint8_t hint_byte = static_cast<uint8_t>(strategy) |
                      static_cast<uint8_t>(baseline) << 2 |
                      static_cast<uint8_t>(top_tier) << 4;
  DCHECK_NE(hint_byte, kNoCompilationHint);
  hint_ = hint_byte;
}

void WasmFunctionBuilder::DeleteCodeAfter(size_t position) {
  DCHECK_LE(position, body_.size());
  body_.Truncate(position);
}

void WasmFunctionBuilder::WriteSignature(ZoneBuffer* buffer) const {
  buffer->write_u32v(signature_index_);
}

void WasmFunctionBuilder::WriteBody(ZoneBuffer* buffer) const {
  size_t locals_size = locals_.Size();
  buffer->write_size(locals_size + body_.size());
  buffer->EnsureSpace(locals_size);
  byte** ptr = buffer->pos_ptr();
  locals_.Emit(*ptr);
  (*ptr) += locals_size;  // UGLY: manual bump of position pointer
  if (body_.size() > 0) {
    buffer->write(body_.begin(), body_.size());
  }
}

void WasmFunctionBuilder::WriteAsmWasmOffsetTable(ZoneBuffer* buffer) const {
  if (asm_func_start_source_position_ == 0 && asm_offsets_.size() == 0) {
    buffer->write_size(0);
    return;
  }
  size_t locals_enc_size = LEBHelper::sizeof_u32v(locals_.Size());
  size_t func_start_size =
      LEBHelper::sizeof_u32v(asm_func_start_source_position_);
  buffer->write_size(asm_offsets_.size() + locals_enc_size + func_start_size);
  // Offset of the recorded byte offsets.
  DCHECK_GE(kMaxUInt32, locals_.Size());
  buffer->write_u32v(static_cast<uint32_t>(locals_.Size()));
  // Start position of the function.
  buffer->write_u32v(asm_func_start_source_position_);
  buffer->write(asm_offsets_.begin(), asm_offsets_.size());
}

WasmFunctionBuilder* WasmModuleBuilder::AddFunction(const FunctionSig* sig) {
  functions.push_back(this->zone()->New<WasmFunctionBuilder>(this));
  // Add the signature if one was provided here.
  if (sig) functions.back()->SetSignature(sig);
  return functions.back();
}

uint32_t WasmModuleBuilder::AddImportedFunction(
    base::Vector<const char> name, FunctionSig* sig,
    base::Vector<const char> module) {
  AddFunction(sig);
  uint32_t index = static_cast<uint32_t>(functions.size() - 1);
  import_table.emplace_back(module, name,
                            ImportExportKindCode::kExternalFunction, index);
  num_imported_functions++;
  return index;
}

void WasmModuleBuilder::SetIndirectFunction(uint32_t table_index,
                                            uint32_t index_in_table,
                                            uint32_t direct_function_index) {
  using WasmElemSegment = WasmElemSegmentAbstract<WasmInitExpr>;
  WasmElemSegment segment(kWasmFuncRef, table_index,
                          WasmInitExpr(static_cast<int>(index_in_table)),
                          WasmElemSegment::kFunctionIndexElements);
  segment.entries.emplace_back(
      WasmInitExpr::RefFuncConst(direct_function_index));
  elem_segments.push_back(std::move(segment));
}

namespace {
void WriteInitializerExpressionWithEnd(ZoneBuffer* buffer,
                                       const WasmInitExpr& init,
                                       ValueType type) {
  switch (init.kind()) {
    case WasmInitExpr::kI32Const:
      buffer->write_u8(kExprI32Const);
      buffer->write_i32v(init.immediate().i32_const);
      break;
    case WasmInitExpr::kI64Const:
      buffer->write_u8(kExprI64Const);
      buffer->write_i64v(init.immediate().i64_const);
      break;
    case WasmInitExpr::kF32Const:
      buffer->write_u8(kExprF32Const);
      buffer->write_f32(init.immediate().f32_const);
      break;
    case WasmInitExpr::kF64Const:
      buffer->write_u8(kExprF64Const);
      buffer->write_f64(init.immediate().f64_const);
      break;
    case WasmInitExpr::kS128Const:
      buffer->write_u8(kSimdPrefix);
      buffer->write_u8(kExprS128Const & 0xFF);
      buffer->write(init.immediate().s128_const.data(), kSimd128Size);
      break;
    case WasmInitExpr::kGlobalGet:
      buffer->write_u8(kExprGlobalGet);
      buffer->write_u32v(init.immediate().index);
      break;
    case WasmInitExpr::kRefNullConst:
      buffer->write_u8(kExprRefNull);
      buffer->write_i32v(HeapType(init.immediate().heap_type).code());
      break;
    case WasmInitExpr::kRefFuncConst:
      buffer->write_u8(kExprRefFunc);
      buffer->write_u32v(init.immediate().index);
      break;
    case WasmInitExpr::kNone: {
      // No initializer, emit a default value.
      switch (type.kind()) {
        case kI32:
          buffer->write_u8(kExprI32Const);
          // LEB encoding of 0.
          buffer->write_u8(0);
          break;
        case kI64:
          buffer->write_u8(kExprI64Const);
          // LEB encoding of 0.
          buffer->write_u8(0);
          break;
        case kF32:
          buffer->write_u8(kExprF32Const);
          buffer->write_f32(0.f);
          break;
        case kF64:
          buffer->write_u8(kExprF64Const);
          buffer->write_f64(0.);
          break;
        case kOptRef:
          buffer->write_u8(kExprRefNull);
          buffer->write_i32v(type.heap_type().code());
          break;
        case kS128:
          buffer->write_u8(static_cast<byte>(kSimdPrefix));
          buffer->write_u8(static_cast<byte>(kExprS128Const & 0xff));
          for (int i = 0; i < kSimd128Size; i++) buffer->write_u8(0);
          break;
        case kI8:
        case kI16:
        case kVoid:
        case kBottom:
        case kRef:
        case kRtt:
          UNREACHABLE();
      }
      break;
    }
    case WasmInitExpr::kStructNew:
    case WasmInitExpr::kStructNewWithRtt:
    case WasmInitExpr::kStructNewDefault:
    case WasmInitExpr::kStructNewDefaultWithRtt:
      STATIC_ASSERT((kExprStructNew >> 8) == kGCPrefix);
      STATIC_ASSERT((kExprStructNewWithRtt >> 8) == kGCPrefix);
      STATIC_ASSERT((kExprStructNewDefault >> 8) == kGCPrefix);
      STATIC_ASSERT((kExprStructNewDefaultWithRtt >> 8) == kGCPrefix);
      for (const WasmInitExpr& operand : *init.operands()) {
        WriteInitializerExpressionWithEnd(buffer, operand, kWasmBottom);
      }
      buffer->write_u8(kGCPrefix);
      WasmOpcode opcode;
      switch (init.kind()) {
        case WasmInitExpr::kStructNewWithRtt:
          opcode = kExprStructNewWithRtt;
          break;
        case WasmInitExpr::kStructNew:
          opcode = kExprStructNew;
          break;
        case WasmInitExpr::kStructNewDefaultWithRtt:
          opcode = kExprStructNewDefaultWithRtt;
          break;
        case WasmInitExpr::kStructNewDefault:
          opcode = kExprStructNewDefault;
          break;
        default:
          UNREACHABLE();
      }
      buffer->write_u8(static_cast<uint8_t>(opcode));
      buffer->write_u32v(init.immediate().index);
      break;
    case WasmInitExpr::kArrayInit:
    case WasmInitExpr::kArrayInitStatic:
      STATIC_ASSERT((kExprArrayInit >> 8) == kGCPrefix);
      STATIC_ASSERT((kExprArrayInitStatic >> 8) == kGCPrefix);
      for (const WasmInitExpr& operand : *init.operands()) {
        WriteInitializerExpressionWithEnd(buffer, operand, kWasmBottom);
      }
      buffer->write_u8(kGCPrefix);
      buffer->write_u8(static_cast<uint8_t>(
          init.kind() == WasmInitExpr::kArrayInit ? kExprArrayInit
                                                  : kExprArrayInitStatic));
      buffer->write_u32v(init.immediate().index);
      buffer->write_u32v(static_cast<uint32_t>(init.operands()->size() - 1));
      break;
    case WasmInitExpr::kRttCanon:
      STATIC_ASSERT((kExprRttCanon >> 8) == kGCPrefix);
      buffer->write_u8(kGCPrefix);
      buffer->write_u8(static_cast<uint8_t>(kExprRttCanon));
      buffer->write_i32v(static_cast<int32_t>(init.immediate().index));
      break;
  }
}

void WriteInitializerExpression(ZoneBuffer* buffer, const WasmInitExpr& init,
                                ValueType type) {
  WriteInitializerExpressionWithEnd(buffer, init, type);
  buffer->write_u8(kExprEnd);
}
}  // namespace

void WasmModuleBuilder::WriteTo(ZoneBuffer* buffer) const {
  // == Emit magic =============================================================
  buffer->write_u32(kWasmMagic);
  buffer->write_u32(kWasmVersion);

  // == Emit types =============================================================
  if (types.size() > 0) {
    size_t start = EmitSection(kTypeSectionCode, buffer);
    buffer->write_size(types.size());

    // TODO(7748): Add support for recursive groups.
    for (const TypeDefinition& type : types) {
      if (type.supertype != kNoSuperType) {
        buffer->write_u8(kWasmSubtypeCode);
        buffer->write_u8(1);  // The supertype count is always 1.
        buffer->write_u32v(type.supertype);
      }
      switch (type.kind) {
        case TypeDefinition::kFunction: {
          const FunctionSig* sig = type.function_sig;
          buffer->write_u8(kWasmFunctionTypeCode);
          buffer->write_size(sig->parameter_count());
          for (auto param : sig->parameters()) {
            WriteValueType(buffer, param);
          }
          buffer->write_size(sig->return_count());
          for (auto ret : sig->returns()) {
            WriteValueType(buffer, ret);
          }
          break;
        }
        case TypeDefinition::kStruct: {
          const StructType* struct_type = type.struct_type;
          buffer->write_u8(kWasmStructTypeCode);
          buffer->write_size(struct_type->field_count());
          for (uint32_t i = 0; i < struct_type->field_count(); i++) {
            WriteValueType(buffer, struct_type->field(i));
            buffer->write_u8(struct_type->mutability(i) ? 1 : 0);
          }
          break;
        }
        case TypeDefinition::kArray: {
          const ArrayType* array_type = type.array_type;
          buffer->write_u8(kWasmArrayTypeCode);
          WriteValueType(buffer, array_type->element_type());
          buffer->write_u8(array_type->mutability() ? 1 : 0);
          break;
        }
      }
    }
    FixupSection(buffer, start);
  }

  // == Emit imports ===========================================================
  if (import_table.size() > 0) {
    size_t start = EmitSection(kImportSectionCode, buffer);
    buffer->write_size(import_table.size());
    for (auto import : import_table) {
      buffer->write_string(import.module_name);
      buffer->write_string(import.field_name);
      buffer->write_u8(import.kind);
      switch (import.kind) {
        case kExternalFunction:
          buffer->write_u32v(functions[import.index]->signature_index_);
          break;
        case kExternalGlobal:
          WriteValueType(buffer, globals[import.index].type);
          buffer->write_u8(globals[import.index].mutability ? 1 : 0);
          break;
        case kExternalTable:
        case kExternalMemory:
        case kExternalTag:
          UNREACHABLE();
      }
    }
    FixupSection(buffer, start);
  }

  // == Emit function signatures ===============================================
  uint32_t num_function_names = 0;
  if (functions.size() - num_imported_functions > 0) {
    size_t start = EmitSection(kFunctionSectionCode, buffer);
    buffer->write_size(functions.size() - num_imported_functions);
    for (uint32_t i = num_imported_functions; i < functions.size(); i++) {
      auto* function = functions[i];
      function->WriteSignature(buffer);
      if (!function->name_.empty()) ++num_function_names;
    }
    FixupSection(buffer, start);
  }

  // == Emit tables ============================================================
  if (tables.size() > 0) {
    size_t start = EmitSection(kTableSectionCode, buffer);
    buffer->write_size(tables.size());
    for (const auto& table : tables) {
      WriteValueType(buffer, table.type);
      buffer->write_u8(table.has_maximum_size ? kWithMaximum : kNoMaximum);
      buffer->write_size(table.initial_size);
      if (table.has_maximum_size) buffer->write_size(table.maximum_size);
      if (table.initial_value.kind() != WasmInitExpr::kNone) {
        WriteInitializerExpression(buffer, table.initial_value, table.type);
      }
    }
    FixupSection(buffer, start);
  }

  // == Emit memory declaration ================================================
  {
    size_t start = EmitSection(kMemorySectionCode, buffer);
    buffer->write_u8(1);  // memory count
    if (has_shared_memory) {
      buffer->write_u8(has_maximum_pages ? kSharedWithMaximum
                                         : kSharedNoMaximum);
    } else {
      buffer->write_u8(has_maximum_pages ? kWithMaximum : kNoMaximum);
    }
    buffer->write_u32v(initial_pages);
    if (has_maximum_pages) {
      buffer->write_u32v(maximum_pages);
    }
    FixupSection(buffer, start);
  }

  // == Emit event section =====================================================
  if (tags.size() > 0) {
    size_t start = EmitSection(kTagSectionCode, buffer);
    buffer->write_size(tags.size());
    for (auto& tag : tags) {
      buffer->write_u32v(kExceptionAttribute);
      buffer->write_u32v(signature_map.Find(*tag.sig));
    }
    FixupSection(buffer, start);
  }

  // == Emit globals ===========================================================
  if (globals.size() > 0) {
    size_t start = EmitSection(kGlobalSectionCode, buffer);
    size_t num_imported_globals =
        std::count_if(globals.begin(), globals.end(),
                      [](auto global) -> bool { return global.imported; });

    buffer->write_size(globals.size() - num_imported_globals);

    for (size_t i = num_imported_globals; i < globals.size(); i++) {
      const auto& global = globals[i];
      WriteValueType(buffer, global.type);
      buffer->write_u8(global.mutability ? 1 : 0);
      WriteInitializerExpression(buffer, global.init, global.type);
    }
    FixupSection(buffer, start);
  }

  // == emit exports ===========================================================
  if (export_table.size() > 0) {
    size_t start = EmitSection(kExportSectionCode, buffer);
    buffer->write_size(export_table.size());
    for (auto ex : export_table) {
      buffer->write_string(ex.name);
      buffer->write_u8(ex.kind);
      buffer->write_size(ex.index);
    }
    FixupSection(buffer, start);
  }

  // == emit start function index ==============================================
  if (start_function_index >= 0) {
    size_t start = EmitSection(kStartSectionCode, buffer);
    buffer->write_size(start_function_index);
    FixupSection(buffer, start);
  }

  // == emit element segments ==================================================
  if (elem_segments.size() > 0) {
    size_t start = EmitSection(kElementSectionCode, buffer);
    buffer->write_size(elem_segments.size());
    using WasmElemSegment = WasmElemSegmentAbstract<WasmInitExpr>;
    for (const WasmElemSegment& segment : elem_segments) {
      bool is_active = segment.status == WasmElemSegment::kStatusActive;
      // We pick the most general syntax, i.e., we always explicitly emit the
      // table index and the type, and use the expressions-as-elements syntax.
      // The initial byte is one of 0x05, 0x06, and 0x07.
      uint8_t kind_mask =
          segment.status == WasmElemSegment::kStatusActive
              ? 0b10
              : segment.status == WasmElemSegment::kStatusDeclarative ? 0b11
                                                                      : 0b01;
      uint8_t expressions_as_elements_mask = 0b100;
      buffer->write_u8(kind_mask | expressions_as_elements_mask);
      if (is_active) {
        buffer->write_u32v(segment.table_index);
        WriteInitializerExpression(buffer, segment.offset, segment.type);
      }
      WriteValueType(buffer, segment.type);
      buffer->write_size(segment.entries.size());
      for (const WasmInitExpr& entry : segment.entries) {
        WriteInitializerExpression(buffer, entry, segment.type);
      }
    }
    FixupSection(buffer, start);
  }
  /*
    // == emit compilation hints section
    ========================================= bool emit_compilation_hints =
    false; for (auto* fn : functions_) { if (fn->hint_ != kNoCompilationHint) {
        emit_compilation_hints = true;
        break;
      }
    }
    if (emit_compilation_hints) {
      // Emit the section code.
      buffer->write_u8(kUnknownSectionCode);
      // Emit a placeholder for section length.
      size_t start = buffer->reserve_u32v();
      // Emit custom section name.
      buffer->write_string(base::CStrVector("compilationHints"));
      // Emit hint count.
      buffer->write_size(functions_.size());
      // Emit hint bytes.
      for (auto* fn : functions_) {
        uint8_t hint_byte =
            fn->hint_ != kNoCompilationHint ? fn->hint_ :
    kDefaultCompilationHint; buffer->write_u8(hint_byte);
      }
      FixupSection(buffer, start);
    }*/

  // == emit code ==============================================================
  if (functions.size() - num_imported_functions > 0) {
    size_t start = EmitSection(kCodeSectionCode, buffer);
    buffer->write_size(functions.size() - num_imported_functions);
    for (size_t i = num_imported_functions; i < functions.size(); i++) {
      functions[i]->WriteBody(buffer);
    }
    FixupSection(buffer, start);
  }

  // == emit data segments =====================================================
  if (data_segments.size() > 0) {
    size_t start = EmitSection(kDataSectionCode, buffer);
    buffer->write_size(data_segments.size());

    for (auto segment : data_segments) {
      buffer->write_u8(0);              // linear memory segment
      WriteInitializerExpression(buffer, segment.dest_addr, kWasmI32);
      buffer->write_u32v(static_cast<uint32_t>(segment.source.size()));
      buffer->write_string(segment.source);
    }
    FixupSection(buffer, start);
  }
  /*
    // == Emit names
    ============================================================= if
    (num_function_names > 0 || !function_imports_.empty()) {
      // Emit the section code.
      buffer->write_u8(kUnknownSectionCode);
      // Emit a placeholder for the length.
      size_t start = buffer->reserve_u32v();
      // Emit the section string.
      buffer->write_string(base::CStrVector("name"));
      // Emit a subsection for the function names.
      buffer->write_u8(NameSectionKindCode::kFunctionCode);
      // Emit a placeholder for the subsection length.
      size_t functions_start = buffer->reserve_u32v();
      // Emit the function names.
      // Imports are always named.
      uint32_t num_imports = static_cast<uint32_t>(function_imports_.size());
      buffer->write_size(num_imports + num_function_names);
      uint32_t function_index = 0;
      for (; function_index < num_imports; ++function_index) {
        const WasmFunctionImport* import = &function_imports_[function_index];
        DCHECK(!import->name.empty());
        buffer->write_u32v(function_index);
        buffer->write_string(import->name);
      }
      if (num_function_names > 0) {
        for (auto* function : functions_) {
          DCHECK_EQ(function_index,
                    function->func_index() + function_imports_.size());
          if (!function->name_.empty()) {
            buffer->write_u32v(function_index);
            buffer->write_string(function->name_);
          }
          ++function_index;
        }
      }
      FixupSection(buffer, functions_start);
      FixupSection(buffer, start);
    }*/
}

void WasmModuleBuilder::WriteAsmJsOffsetTable(ZoneBuffer* buffer) const {
  // == Emit asm.js offset table ===============================================
  buffer->write_size(functions.size() - num_imported_functions);
  // Emit the offset table per function.
  for (size_t i = num_imported_functions; i < functions.size(); i++) {
    functions[i]->WriteBody(buffer);
  }
}
}  // namespace wasm
}  // namespace internal
}  // namespace v8
