// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/parsing/preparsed-scope-data.h"

#include "src/ast/scopes.h"
#include "src/ast/variables.h"
#include "src/handles.h"
#include "src/objects-inl.h"
#include "src/objects/shared-function-info.h"
#include "src/parsing/preparser.h"

namespace v8 {
namespace internal {

namespace {

class ScopeCallsSloppyEvalField : public BitField<bool, 0, 1> {};
class InnerScopeCallsEvalField
    : public BitField<bool, ScopeCallsSloppyEvalField::kNext, 1> {};

class VariableMaybeAssignedField : public BitField8<bool, 0, 1> {};
class VariableContextAllocatedField
    : public BitField8<bool, VariableMaybeAssignedField::kNext, 1> {};

const int kMagicValue = 0xC0DE0DE;

#ifdef DEBUG
const size_t kUint32Size = 5;
const size_t kUint8Size = 2;
const size_t kQuarterMarker = 0;
#else
const size_t kUint32Size = 4;
const size_t kUint8Size = 1;
#endif

const int kPlaceholderSize = kUint32Size;
const int kSkippableFunctionDataSize = 2 * kUint32Size + 1 * kUint8Size;

class LanguageField : public BitField8<LanguageMode, 0, 1> {};
class UsesSuperField : public BitField8<bool, LanguageField::kNext, 1> {};
STATIC_ASSERT(LanguageModeSize <= LanguageField::kNumValues);

}  // namespace

/*

  Internal data format for the backing store of ProducedUncompiledData and
  UncompiledDataWithScope::scope_data (on the heap):

  (Skippable function data:)
  ------------------------------------
  | scope_data_start                 |
  ------------------------------------
  | data for inner function 1        |
  | ...                              |
  ------------------------------------
  | data for inner function n        |
  | ...                              |
  ------------------------------------
  (Scope allocation data:)             << scope_data_start points here
  ------------------------------------
  magic value
  ------------------------------------
  | scope type << only in debug      |
  | eval                             |
  | ----------------------           |
  | | data for variables |           |
  | | ...                |           |
  | ----------------------           |
  ------------------------------------
  ------------------------------------
  | data for inner scope 1           | << but not for function scopes
  | ...                              |
  ------------------------------------
  ...
  ------------------------------------
  | data for inner scope m           |
  | ...                              |
  ------------------------------------

  UncompiledDataWithScope::child_data is an array of UncompiledData objects, one
  for each skippable inner function.

  ConsumedUncompiledData wraps a UncompiledDataWithScope and reads data from it.

 */

void ProducedUncompiledData::ByteData::WriteUint32(uint32_t data) {
#ifdef DEBUG
  // Save expected item size in debug mode.
  backing_store_.push_back(kUint32Size);
#endif
  const uint8_t* d = reinterpret_cast<uint8_t*>(&data);
  for (int i = 0; i < 4; ++i) {
    backing_store_.push_back(*d++);
  }
  free_quarters_in_last_byte_ = 0;
}

void ProducedUncompiledData::ByteData::OverwriteFirstUint32(uint32_t data) {
  auto it = backing_store_.begin();
#ifdef DEBUG
  // Check that that position already holds an item of the expected size.
  DCHECK_GE(backing_store_.size(), kUint32Size);
  DCHECK_EQ(*it, kUint32Size);
  ++it;
#endif
  const uint8_t* d = reinterpret_cast<uint8_t*>(&data);
  for (size_t i = 0; i < 4; ++i) {
    *it++ = *d++;
  }
}

void ProducedUncompiledData::ByteData::WriteUint8(uint8_t data) {
#ifdef DEBUG
  // Save expected item size in debug mode.
  backing_store_.push_back(kUint8Size);
#endif
  backing_store_.push_back(data);
  free_quarters_in_last_byte_ = 0;
}

void ProducedUncompiledData::ByteData::WriteQuarter(uint8_t data) {
  DCHECK_LE(data, 3);
  if (free_quarters_in_last_byte_ == 0) {
#ifdef DEBUG
    // Save a marker in debug mode.
    backing_store_.push_back(kQuarterMarker);
#endif
    backing_store_.push_back(0);
    free_quarters_in_last_byte_ = 3;
  } else {
    --free_quarters_in_last_byte_;
  }

  uint8_t shift_amount = free_quarters_in_last_byte_ * 2;
  DCHECK_EQ(backing_store_.back() & (3 << shift_amount), 0);
  backing_store_.back() |= (data << shift_amount);
}

Handle<PodArray<uint8_t>> ProducedUncompiledData::ByteData::Serialize(
    Isolate* isolate) {
  Handle<PodArray<uint8_t>> array = PodArray<uint8_t>::New(
      isolate, static_cast<int>(backing_store_.size()), TENURED);

  DisallowHeapAllocation no_gc;
  PodArray<uint8_t>* raw_array = *array;

  int i = 0;
  for (uint8_t item : backing_store_) {
    raw_array->set(i++, item);
  }
  return array;
}

ProducedUncompiledData::ProducedUncompiledData(Zone* zone,
                                               ProducedUncompiledData* parent)
    : parent_(parent),
      byte_data_(new (zone) ByteData(zone)),
      data_for_inner_functions_(zone),
      start_position_(kNoSourcePosition),
      end_position_(kNoSourcePosition),
      bailed_out_(false) {
  if (parent != nullptr) {
    parent->data_for_inner_functions_.push_back(this);
  }
  // Reserve space for scope_data_start, written later:
  byte_data_->WriteUint32(0);
}

// Create a ProducedUncompiledData which is just a proxy for a previous
// produced UncompiledData.
ProducedUncompiledData::ProducedUncompiledData(Handle<UncompiledData> data,
                                               Zone* zone)
    : parent_(nullptr),
      byte_data_(nullptr),
      data_for_inner_functions_(zone),
      start_position_(kNoSourcePosition),
      end_position_(kNoSourcePosition),
      bailed_out_(false),
      previously_produced_uncompiled_data_(data) {}

ProducedUncompiledData::DataGatheringScope::DataGatheringScope(
    DeclarationScope* function_scope, PreParser* preparser)
    : function_scope_(function_scope),
      preparser_(preparser),
      produced_uncompiled_data_(nullptr) {
  if (FLAG_preparser_scope_analysis) {
    ProducedUncompiledData* parent = preparser->produced_uncompiled_data();
    Zone* main_zone = preparser->main_zone();
    produced_uncompiled_data_ =
        new (main_zone) ProducedUncompiledData(main_zone, parent);
    preparser->set_produced_uncompiled_data(produced_uncompiled_data_);
    function_scope->set_produced_uncompiled_data(produced_uncompiled_data_);
  }
}

ProducedUncompiledData::DataGatheringScope::~DataGatheringScope() {
  if (FLAG_preparser_scope_analysis) {
    preparser_->set_produced_uncompiled_data(
        produced_uncompiled_data_->parent_);
  }
}

void ProducedUncompiledData::DataGatheringScope::MarkFunctionAsSkippable(
    int end_position, int num_inner_functions) {
  DCHECK(FLAG_preparser_scope_analysis);
  DCHECK_NOT_NULL(produced_uncompiled_data_);
  DCHECK_NOT_NULL(produced_uncompiled_data_->parent_);
  produced_uncompiled_data_->parent_->AddSkippableFunction(
      function_scope_->start_position(), end_position,
      function_scope_->num_parameters(), num_inner_functions,
      function_scope_->language_mode(), function_scope_->NeedsHomeObject());
}

void ProducedUncompiledData::AddSkippableFunction(int start_position,
                                                  int end_position,
                                                  int num_parameters,
                                                  int num_inner_functions,
                                                  LanguageMode language_mode,
                                                  bool uses_super_property) {
  DCHECK(FLAG_preparser_scope_analysis);
  DCHECK(previously_produced_uncompiled_data_.is_null());

  if (bailed_out_) {
    return;
  }

  // TODO(leszeks): Either remove the start/end position parameters, or DCHECK
  // that they are correct.

  byte_data_->WriteUint32(num_parameters);
  byte_data_->WriteUint32(num_inner_functions);

  uint8_t language_and_super = LanguageField::encode(language_mode) |
                               UsesSuperField::encode(uses_super_property);

  byte_data_->WriteQuarter(language_and_super);
}

void ProducedUncompiledData::SaveScopeData(DeclarationScope* scope) {
  DCHECK(FLAG_preparser_scope_analysis);
  DCHECK(previously_produced_uncompiled_data_.is_null());
  // The data contains a uint32 (reserved space for scope_data_start) and
  // function data items, kSkippableFunctionDataSize each.
  DCHECK_GE(byte_data_->size(), kPlaceholderSize);
  DCHECK_LE(byte_data_->size(), std::numeric_limits<uint32_t>::max());
  DCHECK_EQ(byte_data_->size() % kSkippableFunctionDataSize, kPlaceholderSize);

  // Save the scope's start and end position regardless of whether we produced
  // any other data.
  start_position_ = scope->start_position();
  end_position_ = scope->end_position();

  if (bailed_out_) {
    return;
  }

  uint32_t scope_data_start = static_cast<uint32_t>(byte_data_->size());

  // If there are no skippable inner functions, we don't need to save anything.
  if (scope_data_start == kPlaceholderSize) {
    return;
  }

  byte_data_->OverwriteFirstUint32(scope_data_start);

  // For a data integrity check, write a value between data about skipped inner
  // funcs and data about variables.
  byte_data_->WriteUint32(kMagicValue);

  SaveDataForScope(scope);
}

bool ProducedUncompiledData::ContainsInnerFunctions() const {
  return byte_data_->size() > kPlaceholderSize;
}

Handle<UncompiledData> ProducedUncompiledData::Serialize(Isolate* isolate) {
  if (!previously_produced_uncompiled_data_.is_null()) {
    DCHECK(!bailed_out_);
    DCHECK_EQ(data_for_inner_functions_.size(), 0);
    return previously_produced_uncompiled_data_;
  }

  DCHECK_NE(start_position_, kNoSourcePosition);
  DCHECK_NE(end_position_, kNoSourcePosition);

  if (bailed_out_) {
    return isolate->factory()->NewUncompiledDataWithoutScope(start_position_,
                                                             end_position_);
  }

  DCHECK(!ThisOrParentBailedOut());

  if (byte_data_->size() <= kPlaceholderSize) {
    // The data contains only the placeholder.
    return isolate->factory()->NewUncompiledDataWithoutScope(start_position_,
                                                             end_position_);
  }

  Handle<UncompiledDataWithScope> data =
      isolate->factory()->NewUncompiledDataWithScope(start_position_,
                                                     end_position_);

  Handle<PodArray<uint8_t>> scope_data_array = byte_data_->Serialize(isolate);
  data->set_scope_data(*scope_data_array);

  int child_data_length = static_cast<int>(data_for_inner_functions_.size());
  if (child_data_length == 0) {
    data->set_child_data(*(isolate->factory()->empty_fixed_array()));
  } else {
    Handle<FixedArray> child_array =
        isolate->factory()->NewFixedArray(child_data_length, TENURED);
    int i = 0;
    for (const auto& item : data_for_inner_functions_) {
      Handle<UncompiledData> child_data = item->Serialize(isolate);
      child_array->set(i++, *child_data);
    }
    data->set_child_data(*child_array);
  }

  return data;
}

bool ProducedUncompiledData::ScopeNeedsData(Scope* scope) {
  if (scope->scope_type() == ScopeType::FUNCTION_SCOPE) {
    // Default constructors don't need data (they cannot contain inner functions
    // defined by the user). Other functions do.
    return !IsDefaultConstructor(scope->AsDeclarationScope()->function_kind());
  }
  if (!scope->is_hidden()) {
    for (Variable* var : *scope->locals()) {
      if (IsDeclaredVariableMode(var->mode())) {
        return true;
      }
    }
  }
  for (Scope* inner = scope->inner_scope(); inner != nullptr;
       inner = inner->sibling()) {
    if (ScopeNeedsData(inner)) {
      return true;
    }
  }
  return false;
}

bool ProducedUncompiledData::ScopeIsSkippableFunctionScope(Scope* scope) {
  // Lazy non-arrow function scopes are skippable. Lazy functions are exactly
  // those Scopes which have their own ProducedUncompiledData object. This
  // logic ensures that the scope allocation data is consistent with the
  // skippable function data (both agree on where the lazy function boundaries
  // are).
  if (scope->scope_type() != ScopeType::FUNCTION_SCOPE) {
    return false;
  }
  DeclarationScope* declaration_scope = scope->AsDeclarationScope();
  return !declaration_scope->is_arrow_scope() &&
         declaration_scope->produced_uncompiled_data() != nullptr;
}

void ProducedUncompiledData::SaveDataForScope(Scope* scope) {
  DCHECK_NE(scope->end_position(), kNoSourcePosition);

  if (!ScopeNeedsData(scope)) {
    return;
  }

#ifdef DEBUG
  byte_data_->WriteUint8(scope->scope_type());
#endif

  uint8_t eval =
      ScopeCallsSloppyEvalField::encode(
          scope->is_declaration_scope() &&
          scope->AsDeclarationScope()->calls_sloppy_eval()) |
      InnerScopeCallsEvalField::encode(scope->inner_scope_calls_eval());
  byte_data_->WriteUint8(eval);

  if (scope->scope_type() == ScopeType::FUNCTION_SCOPE) {
    Variable* function = scope->AsDeclarationScope()->function_var();
    if (function != nullptr) {
      SaveDataForVariable(function);
    }
  }

  for (Variable* var : *scope->locals()) {
    if (IsDeclaredVariableMode(var->mode())) {
      SaveDataForVariable(var);
    }
  }

  SaveDataForInnerScopes(scope);
}

void ProducedUncompiledData::SaveDataForVariable(Variable* var) {
#ifdef DEBUG
  // Store the variable name in debug mode; this way we can check that we
  // restore data to the correct variable.
  const AstRawString* name = var->raw_name();
  byte_data_->WriteUint8(name->is_one_byte());
  byte_data_->WriteUint32(name->length());
  for (int i = 0; i < name->length(); ++i) {
    byte_data_->WriteUint8(name->raw_data()[i]);
  }
#endif
  byte variable_data = VariableMaybeAssignedField::encode(
                           var->maybe_assigned() == kMaybeAssigned) |
                       VariableContextAllocatedField::encode(
                           var->has_forced_context_allocation());
  byte_data_->WriteQuarter(variable_data);
}

void ProducedUncompiledData::SaveDataForInnerScopes(Scope* scope) {
  // Inner scopes are stored in the reverse order, but we'd like to write the
  // data in the logical order. There might be many inner scopes, so we don't
  // want to recurse here.
  std::vector<Scope*> scopes;
  for (Scope* inner = scope->inner_scope(); inner != nullptr;
       inner = inner->sibling()) {
    if (ScopeIsSkippableFunctionScope(inner)) {
      // Don't save data about function scopes, since they'll have their own
      // ProducedUncompiledData where their data is saved.
      DCHECK_NOT_NULL(inner->AsDeclarationScope()->produced_uncompiled_data());
      continue;
    }
    scopes.push_back(inner);
  }
  for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
    SaveDataForScope(*it);
  }
}

ConsumedUncompiledData::ByteData::ReadingScope::ReadingScope(
    ConsumedUncompiledData* parent)
    : ReadingScope(parent->scope_data_.get(), parent->data_->scope_data()) {}

int32_t ConsumedUncompiledData::ByteData::ReadUint32() {
  DCHECK_NOT_NULL(data_);
  DCHECK_GE(RemainingBytes(), kUint32Size);
#ifdef DEBUG
  // Check that there indeed is an integer following.
  DCHECK_EQ(data_->get(index_++), kUint32Size);
#endif
  int32_t result = 0;
  byte* p = reinterpret_cast<byte*>(&result);
  for (int i = 0; i < 4; ++i) {
    *p++ = data_->get(index_++);
  }
  stored_quarters_ = 0;
  return result;
}

uint8_t ConsumedUncompiledData::ByteData::ReadUint8() {
  DCHECK_NOT_NULL(data_);
  DCHECK_GE(RemainingBytes(), kUint8Size);
#ifdef DEBUG
  // Check that there indeed is a byte following.
  DCHECK_EQ(data_->get(index_++), kUint8Size);
#endif
  stored_quarters_ = 0;
  return data_->get(index_++);
}

uint8_t ConsumedUncompiledData::ByteData::ReadQuarter() {
  DCHECK_NOT_NULL(data_);
  if (stored_quarters_ == 0) {
    DCHECK_GE(RemainingBytes(), kUint8Size);
#ifdef DEBUG
    // Check that there indeed are quarters following.
    DCHECK_EQ(data_->get(index_++), kQuarterMarker);
#endif
    stored_byte_ = data_->get(index_++);
    stored_quarters_ = 4;
  }
  // Read the first 2 bits from stored_byte_.
  uint8_t result = (stored_byte_ >> 6) & 3;
  DCHECK_LE(result, 3);
  --stored_quarters_;
  stored_byte_ <<= 2;
  return result;
}

ConsumedUncompiledData::ConsumedUncompiledData()
    : isolate_(nullptr), scope_data_(new ByteData()), child_index_(0) {}

ConsumedUncompiledData::~ConsumedUncompiledData() {}

void ConsumedUncompiledData::SetData(Isolate* isolate,
                                     Handle<UncompiledDataWithScope> data) {
  DCHECK_NOT_NULL(isolate);
  DCHECK(data->IsUncompiledDataWithScope());
  isolate_ = isolate;
  data_ = data;
#ifdef DEBUG
  ByteData::ReadingScope reading_scope(this);
  int scope_data_start = scope_data_->ReadUint32();
  scope_data_->SetPosition(scope_data_start);
  DCHECK_EQ(scope_data_->ReadUint32(), kMagicValue);
#endif
  // The first data item is scope_data_start. Skip over it.
  scope_data_->SetPosition(kPlaceholderSize);
}

ProducedUncompiledData* ConsumedUncompiledData::GetDataForSkippableFunction(
    Zone* zone, int start_position, int* end_position, int* num_parameters,
    int* num_inner_functions, bool* uses_super_property,
    LanguageMode* language_mode) {
  ByteData::ReadingScope reading_scope(this);
  CHECK_GE(scope_data_->RemainingBytes(), kSkippableFunctionDataSize);
  *num_parameters = scope_data_->ReadUint32();
  *num_inner_functions = scope_data_->ReadUint32();

  uint8_t language_and_super = scope_data_->ReadQuarter();
  *language_mode = LanguageMode(LanguageField::decode(language_and_super));
  *uses_super_property = UsesSuperField::decode(language_and_super);

  // Retrieve the corresponding UncompiledData and associate it to the skipped
  // function. If the skipped functions contains inner functions, those can be
  // skipped when the skipped function is eagerly parsed.
  FixedArray* children = data_->child_data();
  CHECK_GT(children->length(), child_index_);
  UncompiledData* child_data =
      UncompiledData::cast(children->get(child_index_++));

  // Load the start and end position from the child data. The skippable function
  // *must* be the next function in the data. Use the start position as a sanity
  // check.
  CHECK_EQ(start_position, child_data->start_position());
  *end_position = child_data->end_position();
  DCHECK_GT(*end_position, start_position);

  return new (zone) ProducedUncompiledData(handle(child_data, isolate_), zone);
}

void ConsumedUncompiledData::RestoreScopeAllocationData(
    DeclarationScope* scope) {
  DCHECK(FLAG_preparser_scope_analysis);
  DCHECK_EQ(scope->scope_type(), ScopeType::FUNCTION_SCOPE);
  DCHECK(!data_.is_null());

  ByteData::ReadingScope reading_scope(this);

  int magic_value_from_data = scope_data_->ReadUint32();
  // Check that we've consumed all inner function data.
  CHECK_EQ(magic_value_from_data, kMagicValue);

  int start_position_from_data = data_->start_position();
  int end_position_from_data = data_->end_position();
  CHECK_EQ(start_position_from_data, scope->start_position());
  CHECK_EQ(end_position_from_data, scope->end_position());

  RestoreData(scope);

  // Check that we consumed all scope data.
  DCHECK_EQ(scope_data_->RemainingBytes(), 0);
}

void ConsumedUncompiledData::RestoreData(Scope* scope) {
  if (scope->is_declaration_scope() &&
      scope->AsDeclarationScope()->is_skipped_function()) {
    return;
  }

  // It's possible that scope is not present in the data at all (since PreParser
  // doesn't create the corresponding scope). In this case, the Scope won't
  // contain any variables for which we need the data.
  if (!ProducedUncompiledData::ScopeNeedsData(scope)) {
    return;
  }

  if (scope_data_->RemainingBytes() < kUint8Size) {
    // Temporary debugging code for detecting inconsistent data. Write debug
    // information on the stack, then crash.
    data_->GetIsolate()->PushStackTraceAndDie();
  }

  // scope_type is stored only in debug mode.
  CHECK_GE(scope_data_->RemainingBytes(), kUint8Size);
  DCHECK_EQ(scope_data_->ReadUint8(), scope->scope_type());

  uint32_t eval = scope_data_->ReadUint8();
  if (ScopeCallsSloppyEvalField::decode(eval)) {
    scope->RecordEvalCall();
  }
  if (InnerScopeCallsEvalField::decode(eval)) {
    scope->RecordInnerScopeEvalCall();
  }

  if (scope->scope_type() == ScopeType::FUNCTION_SCOPE) {
    Variable* function = scope->AsDeclarationScope()->function_var();
    if (function != nullptr) {
      RestoreDataForVariable(function);
    }
  }

  for (Variable* var : *scope->locals()) {
    if (IsDeclaredVariableMode(var->mode())) {
      RestoreDataForVariable(var);
    }
  }

  RestoreDataForInnerScopes(scope);
}

void ConsumedUncompiledData::RestoreDataForVariable(Variable* var) {
#ifdef DEBUG
  const AstRawString* name = var->raw_name();
  bool data_one_byte = scope_data_->ReadUint8();
  DCHECK_IMPLIES(name->is_one_byte(), data_one_byte);
  DCHECK_EQ(scope_data_->ReadUint32(), static_cast<uint32_t>(name->length()));
  if (!name->is_one_byte() && data_one_byte) {
    // It's possible that "name" is a two-byte representation of the string
    // stored in the data.
    for (int i = 0; i < 2 * name->length(); i += 2) {
#if defined(V8_TARGET_LITTLE_ENDIAN)
      DCHECK_EQ(scope_data_->ReadUint8(), name->raw_data()[i]);
      DCHECK_EQ(0, name->raw_data()[i + 1]);
#else
      DCHECK_EQ(scope_data_->ReadUint8(), name->raw_data()[i + 1]);
      DCHECK_EQ(0, name->raw_data()[i]);
#endif  // V8_TARGET_LITTLE_ENDIAN
    }
  } else {
    for (int i = 0; i < name->length(); ++i) {
      DCHECK_EQ(scope_data_->ReadUint8(), name->raw_data()[i]);
    }
  }
#endif
  uint8_t variable_data = scope_data_->ReadQuarter();
  if (VariableMaybeAssignedField::decode(variable_data)) {
    var->set_maybe_assigned();
  }
  if (VariableContextAllocatedField::decode(variable_data)) {
    var->set_is_used();
    var->ForceContextAllocation();
  }
}

void ConsumedUncompiledData::RestoreDataForInnerScopes(Scope* scope) {
  std::vector<Scope*> scopes;
  for (Scope* inner = scope->inner_scope(); inner != nullptr;
       inner = inner->sibling()) {
    scopes.push_back(inner);
  }
  for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
    RestoreData(*it);
  }
}

}  // namespace internal
}  // namespace v8
