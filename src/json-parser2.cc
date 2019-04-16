// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/json-parser2.h"

#include "src/char-predicates-inl.h"
#include "src/objects/string-inl.h"
#include "src/zone/zone-list-inl.h"

namespace v8 {
namespace internal {

namespace {

static const int kMaxAscii = 127;

constexpr JsonToken GetOneCharToken(char c) {
  // clang-format off
  return
     c == '"' ? JsonToken::STRING :
     IsDecimalDigit(c) ?  JsonToken::NUMBER :
     c == '-' ? JsonToken::NEGATIVE_NUMBER :
     c == '[' ? JsonToken::LBRACK :
     c == '{' ? JsonToken::LBRACE :
     c == ']' ? JsonToken::RBRACK :
     c == '}' ? JsonToken::RBRACE :
     c == 't' ? JsonToken::TRUE_LITERAL :
     c == 'f' ? JsonToken::FALSE_LITERAL :
     c == 'n' ? JsonToken::NULL_LITERAL :
     c == ' ' ? JsonToken::WHITESPACE :
     c == '\t' ? JsonToken::WHITESPACE :
     c == '\r' ? JsonToken::WHITESPACE :
     c == '\n' ? JsonToken::WHITESPACE :
     c == ':' ? JsonToken::COLON :
     c == ',' ? JsonToken::COMMA :
     JsonToken::ILLEGAL;
  // clang-format on
}

// Table of one-character tokens, by character (0x00..0x7F only).
static const constexpr JsonToken one_char_tokens[128] = {
#define CALL_GET_SCAN_FLAGS(N) GetOneCharToken(N),
    INT_0_TO_127_LIST(CALL_GET_SCAN_FLAGS)
#undef CALL_GET_SCAN_FLAGS
};

enum class EscapeKind : uint8_t {
  ILLEGAL,
  SELF,
  BACKSPACE,
  TAB,
  NEW_LINE,
  FORM_FEED,
  CARRIAGE_RETURN,
  UNICODE
};

class EscapeKindField : public BitField8<EscapeKind, 0, 3> {};
class MayTerminateStringField
    : public BitField8<bool, EscapeKindField::kNext, 1> {};
class NumberPartField
    : public BitField8<bool, MayTerminateStringField::kNext, 1> {};

constexpr bool MayTerminateString(uint8_t flags) {
  return MayTerminateStringField::decode(flags);
}

constexpr EscapeKind GetEscapeKind(uint8_t flags) {
  return EscapeKindField::decode(flags);
}

constexpr bool IsNumberPart(uint8_t flags) {
  return NumberPartField::decode(flags);
}

constexpr uint8_t GetScanFlags(char c) {
  // clang-format off
  return (c == 'b' ? EscapeKindField::encode(EscapeKind::BACKSPACE)
          : c == 't' ? EscapeKindField::encode(EscapeKind::TAB)
          : c == 'n' ? EscapeKindField::encode(EscapeKind::NEW_LINE)
          : c == 'f' ? EscapeKindField::encode(EscapeKind::FORM_FEED)
          : c == 'r' ? EscapeKindField::encode(EscapeKind::CARRIAGE_RETURN)
          : c == 'u' ? EscapeKindField::encode(EscapeKind::UNICODE)
          : c == '"' ? EscapeKindField::encode(EscapeKind::SELF)
          : c == '\\' ? EscapeKindField::encode(EscapeKind::SELF)
          : c == '/' ? EscapeKindField::encode(EscapeKind::SELF)
          : EscapeKindField::encode(EscapeKind::ILLEGAL)) |
         (c < 0x20 ? MayTerminateStringField::encode(true)
          : c == '"' ? MayTerminateStringField::encode(true)
          : c == '\\' ? MayTerminateStringField::encode(true)
          : MayTerminateStringField::encode(false)) |
         NumberPartField::encode(c == '.' ||
                                 c == 'e' ||
                                 c == 'E' ||
                                 IsDecimalDigit(c) ||
                                 c == '-' ||
                                 c == '+');
  // clang-format on
}

// Table of one-character scan flags, by character (0x00..0x7F only).
static const constexpr uint8_t character_scan_flags[128] = {
#define CALL_GET_SCAN_FLAGS(N) GetScanFlags(N),
    INT_0_TO_127_LIST(CALL_GET_SCAN_FLAGS)
#undef CALL_GET_SCAN_FLAGS
};

}  // namespace

template <typename char_type>
void JsonParser2<char_type>::SkipWhitespace() {
  next_ = JsonToken::EOS;

  cursor_ = std::find_if(cursor_, end_, [this](char_type c) {
    JsonToken current =
        V8_LIKELY(c <= kMaxAscii) ? one_char_tokens[c] : JsonToken::ILLEGAL;
    bool result = current != JsonToken::WHITESPACE;
    if (result) next_ = current;
    return result;
  });
}

template <typename char_type>
JsonNumber* JsonParser2<char_type>::ScanNumber(int sign, char_type* start) {
  if (*cursor_ == '0') {
    advance();
    // Prefix zero is only allowed if it's the only digit before
    // a decimal point or exponent.
    if (IsDecimalDigit(*cursor_)) {
      Report(MessageTemplate::kJsonParseUnexpectedToken);
    }
  } else {
    int32_t i = 0;
    int digits = 0;

    char_type* start = cursor_;

    cursor_ = std::find_if(cursor_, end_, [&i, &digits](char_type c) {
      if (!IsDecimalDigit(c)) return true;
      i = i * 10 + (c - '0');
      digits++;
      return false;
    });

    if (cursor_ == start) {
      Report(MessageTemplate::kJsonParseUnexpectedToken);
    }

    if ((is_at_end() || *cursor_ > kMaxAscii ||
         !IsNumberPart(character_scan_flags[*cursor_])) &&
        digits < 10) {
      // Smi.
      // TODO(verwaest): Cache?
      JsonNumber* result = new (zone()) JsonNumber(i * sign);
      json_values_.Add(result);
      return result;
    }
  }

  cursor_ = std::find_if(cursor_, end_, [](char_type c) {
    return !(c <= kMaxAscii && IsNumberPart(character_scan_flags[c]));
  });

  Vector<const uint8_t> chars;
  if (kIsOneByte) {
    chars = Vector<const uint8_t>::cast(
        Vector<const char_type>(start, cursor_ - start));
  } else {
    literal_buffer_.Start();
    while (start++ != cursor_) {
      literal_buffer_.AddChar(*start++);
    }
    chars = literal_buffer_.one_byte_literal();
  }

  double number = StringToDouble(chars,
                                 NO_FLAGS,  // Hex, octal or trailing junk.
                                 std::numeric_limits<double>::quiet_NaN());

  if (std::isnan(number)) {
    Report(MessageTemplate::kJsonParseUnexpectedToken);
  }

  JsonNumber* result = new (zone()) JsonNumber(number);
  json_values_.Add(result);
  return result;
}

template <typename char_type>
JsonParser2<char_type>::JsonParser2(Isolate* isolate)
    : zone_(isolate->allocator(), ZONE_NAME),
      hash_seed_(HashSeed(isolate)),
      string_table_(JsonString::Compare),
      transition_table_(JsonMap::Compare),
      null_("null", isolate->factory()->null_value()),
      true_("true", isolate->factory()->true_value()),
      false_("false", isolate->factory()->false_value()),
      empty_string_(isolate) {
  property_buffer_.resize(kDefaultBufferSize_);
  element_buffer_.resize(kDefaultBufferSize_);

  base::HashMap::Entry* entry =
      string_table_.LookupOrInsert(&empty_string_, empty_string_.hash());
  DCHECK_NULL(entry->value);
  entry->key = &empty_string_;
  entry->value = reinterpret_cast<void*>(1);
}

template <typename char_type>
JsonString* JsonParser2<char_type>::ScanString(JsonString* hint) {
  char_type* start = cursor_;

  // First try to fast scan without buffering in case the string doesn't have
  // escaped sequences. Always buffer two-byte input strings as the scanned
  // substring can be one-byte.
  if (kIsOneByte && V8_LIKELY(!is_at_end())) {
    while (true) {
      cursor_ = std::find_if(cursor_, end_, [](char_type c) {
        return V8_LIKELY(c <= kMaxAscii) &&
               MayTerminateString(character_scan_flags[c]);
      });

      if (V8_UNLIKELY(is_at_end())) break;

      if (*cursor_ == '"') {
        Vector<const char_type> bytes(start, cursor_ - start);
        JsonString* result =
            hint && hint->Matches(bytes)
                ? hint
                : InternalizeOneByteString(Vector<const uint8_t>::cast(bytes));
        advance();
        return result;
      }

      if (*cursor_ == '\\') break;

      DCHECK_LT(*cursor_, 0x20);
      Report(MessageTemplate::kJsonParseUnexpectedToken);
      return &empty_string_;
    }

    if (V8_LIKELY(!is_at_end())) {
      DCHECK(kIsOneByte);
      // We hit an escape sequence. Start buffering.
      literal_buffer_.Start();
      while (start != cursor_) {
        literal_buffer_.AddChar(*start++);
      }
    }
  } else {
    literal_buffer_.Start();
  }

  if (V8_LIKELY(!is_at_end())) {
    while (true) {
      cursor_ = std::find_if(cursor_, end_, [this](char_type c) {
        if (V8_UNLIKELY(c > kMaxAscii)) {
          AddLiteralChar(c);
          return false;
        }
        if (MayTerminateString(character_scan_flags[c])) {
          return true;
        }
        AddLiteralChar(c);
        return false;
      });

      if (V8_UNLIKELY(is_at_end())) break;

      if (*cursor_ == '"') {
        JsonString* result;
        if (literal_buffer_.is_one_byte()) {
          Vector<const uint8_t> bytes = literal_buffer_.one_byte_literal();
          result = hint && hint->Matches(bytes)
                       ? hint
                       : InternalizeOneByteString(bytes);
        } else {
          Vector<const uint16_t> bytes = literal_buffer_.two_byte_literal();
          result = hint && hint->Matches(bytes)
                       ? hint
                       : InternalizeTwoByteString(bytes);
        }
        advance();
        return result;
      }

      if (*cursor_ == '\\') {
        advance();
        if (V8_UNLIKELY(is_at_end())) break;
        char_type current = *cursor_;

        if (V8_UNLIKELY(current > kMaxAscii)) {
          Report(MessageTemplate::kJsonParseUnexpectedToken);
          return &empty_string_;
        }

        uc32 value;

        switch (GetEscapeKind(character_scan_flags[current])) {
          case EscapeKind::SELF:
            value = current;
            break;

          case EscapeKind::BACKSPACE:
            value = '\x08';
            break;

          case EscapeKind::TAB:
            value = '\x09';
            break;

          case EscapeKind::NEW_LINE:
            value = '\x0A';
            break;

          case EscapeKind::FORM_FEED:
            value = '\x0C';
            break;

          case EscapeKind::CARRIAGE_RETURN:
            value = '\x0D';
            break;

          case EscapeKind::UNICODE: {
            value = 0;
            for (int i = 0; i < 4; i++) {
              int digit = HexValue(NextCharacter());
              if (V8_UNLIKELY(digit < 0)) {
                Report(MessageTemplate::kJsonParseUnexpectedToken);
                return &empty_string_;
              }
              value = value * 16 + digit;
            }
            break;
          }

          case EscapeKind::ILLEGAL:
            Report(MessageTemplate::kJsonParseUnexpectedToken);
            return &empty_string_;
        }

        AddLiteralChar(value);
        advance();
        continue;
      }

      DCHECK_LT(*cursor_, 0x20);
      Report(MessageTemplate::kJsonParseUnexpectedToken);
      return &empty_string_;
    }
  }

  Report(MessageTemplate::kJsonParseUnexpectedEOS);
  return &empty_string_;
}

template <typename char_type>
JsonMap* JsonParser2<char_type>::Transition(JsonMap* parent,
                                            JsonString* property) {
  if (parent->expected_transition_property() == property) {
    return parent->expected_transition();
  }

  JsonMap key(parent, property);
  base::HashMap::Entry* entry =
      transition_table_.LookupOrInsert(&key, key.hash());
  if (entry->value == nullptr) {
    JsonMap* new_map = new (zone()) JsonMap(parent, property);
    parent->set_transition(new_map);
    entry->key = new_map;
    entry->value = reinterpret_cast<void*>(1);
  }
  return static_cast<JsonMap*>(entry->key);
}

template <typename char_type>
JsonObject* JsonParser2<char_type>::ParseObject() {
  Consume(JsonToken::LBRACE);

  ScopedPtrList<JsonValue> properties(&property_buffer_);
  ScopedPtrList<JsonValue> elements(&element_buffer_);

  JsonMap* map = &root_map_;

  if (!Check(JsonToken::RBRACE)) {
    do {
      ExpectNext(JsonToken::STRING);
      JsonString* key = ScanString(map->expected_transition_property());
      ExpectNext(JsonToken::COLON);
      JsonValue* value = ParseJsonValue();

      if (key->is_named_property()) {
        map = Transition(map, key);
        map->UpdateFieldType(value);
        properties.Add(value);
      } else {
        elements.Add(value);
      }
    } while (Check(JsonToken::COMMA));
    Expect(JsonToken::RBRACE);
  }

  JsonObject* result =
      new (zone()) JsonObject(zone(), map, properties, elements);
  json_values_.Add(result);
  return result;
}

template <typename char_type>
JsonArray* JsonParser2<char_type>::ParseArray() {
  Consume(JsonToken::LBRACK);

  ScopedPtrList<JsonValue> elements(&element_buffer_);

  if (!Check(JsonToken::RBRACK)) {
    do {
      elements.Add(ParseJsonValue());
    } while (Check(JsonToken::COMMA));
    Expect(JsonToken::RBRACK);
  }

  JsonArray* result = new (zone()) JsonArray(zone(), elements);
  json_values_.Add(result);
  return result;
}

template <typename char_type>
JsonValue* JsonParser2<char_type>::ParseJsonValue() {
  SkipWhitespace();

  switch (peek()) {
    case JsonToken::NUMBER:
      return ScanNumber(1, cursor_);

    case JsonToken::NEGATIVE_NUMBER: {
      return ScanNumber(-1, cursor_++);
    }

    case JsonToken::STRING:
      advance();
      return ScanString(nullptr);

    case JsonToken::LBRACE:
      return ParseObject();

    case JsonToken::LBRACK:
      return ParseArray();

    case JsonToken::TRUE_LITERAL:
      ScanLiteral("true");
      return &true_;

    case JsonToken::FALSE_LITERAL:
      ScanLiteral("false");
      return &false_;

    case JsonToken::NULL_LITERAL:
      ScanLiteral("null");
      return &null_;

    case JsonToken::COLON:
    case JsonToken::COMMA:
    case JsonToken::ILLEGAL:
    case JsonToken::RBRACE:
    case JsonToken::RBRACK:
      Report(MessageTemplate::kJsonParseUnexpectedToken);
      return &null_;

    case JsonToken::EOS:
      Report(MessageTemplate::kJsonParseUnexpectedEOS);
      return &null_;

    case JsonToken::WHITESPACE:
      UNREACHABLE();
  }
}

void JsonValue::Internalize(Isolate* isolate, AllocationType allocation) {
  DCHECK(!is_internalized_);
  switch (kind_) {
    case JsonValue::Kind::OBJECT:
      static_cast<JsonObject*>(this)->Internalize(isolate, allocation);
      break;
    case JsonValue::Kind::ARRAY:
      static_cast<JsonArray*>(this)->Internalize(isolate, allocation);
      break;
    case JsonValue::Kind::STRING:
      static_cast<JsonString*>(this)->Internalize(isolate, allocation);
      break;
    case JsonValue::Kind::NUMBER:
      static_cast<JsonNumber*>(this)->Internalize(isolate, allocation);
      break;
    case JsonValue::Kind::LITERAL:
      UNREACHABLE();
  }
}

void JsonValue::SetObject(Handle<Object> object) {
  DCHECK(!is_internalized_);
  is_internalized_ = true;
  object_ = object.location();
}

template <typename char_type>
Handle<Object> JsonParser2<char_type>::InternalizeJson(Isolate* isolate) {
  JsonValue* last = nullptr;
  for (JsonValue* current = json_values_.first(); current != nullptr;) {
    JsonValue* next = *current->next();
    current->Internalize(isolate, allocation_);
    last = current;
    current = next;
  }
  return last->object();
}

Handle<Map> JsonMap::Internalize(Isolate* isolate, AllocationType allocation) {
  if (!object_.is_null()) return object_;

  object_ = isolate->factory()->ObjectLiteralMapFromCache(
      isolate->native_context(), number_of_properties_);

  if (object_->is_dictionary_map()) return object_;

  // Make transitions point towards this map so we can walk back here.
  JsonMap* map = this;

  while (true) {
    JsonMap* parent = map->parent_;
    if (parent == nullptr) break;
    parent->transition_ = map;
    map = parent;
  }

  // We start at the empty map, adding always the next property. We stop once
  // the last property that was added was introduced by 'this' map.
  while (map != this) {
    map = map->transition_;
    JsonString* property = map->property();
    // JsonMap* field_type_ = map->field_type();
    object_ = Map::TransitionToDataProperty(
        isolate, object_, property->object(),
        isolate->factory()->undefined_value(), NONE, kDefaultFieldConstness,
        StoreOrigin::kNamed);
  }

  return object_;
}

void JsonObject::Internalize(Isolate* isolate, AllocationType allocation) {
  Handle<Map> heap_map = map()->Internalize(isolate, allocation);
  Handle<Object> object =
      isolate->factory()->NewJSObjectFromMap(heap_map, allocation);

  for (int i = 0; i < heap_map->NumberOfOwnDescriptors(); i++) {
    PropertyDetails details = heap_map->instance_descriptors()->GetDetails(i);
    JSObject::cast(*object)->WriteToField(i, details,
                                          *properties_[i]->object());
  }

  SetObject(object);
}

void JsonMap::UpdateFieldType(JsonValue* value) {
  JsonMap* map = nullptr;

  switch (value->kind()) {
    case JsonValue::Kind::STRING:
    case JsonValue::Kind::NUMBER:
    case JsonValue::Kind::LITERAL:
    case JsonValue::Kind::ARRAY:
      break;

    case JsonValue::Kind::OBJECT:
      map = static_cast<JsonObject*>(value)->map();
      break;
  }

  if (seen_field_type_) {
    if (map != field_type_) field_type_ = nullptr;
  } else {
    field_type_ = map;
  }
  seen_field_type_ = true;
}

void JsonArray::Internalize(Isolate* isolate, AllocationType allocation) {
  Handle<FixedArray> elements =
      isolate->factory()->NewFixedArray(elements_.length(), allocation);
  for (int i = 0; i < elements_.length(); i++) {
    elements->set(i, *elements_[i]->object());
  }
  SetObject(isolate->factory()->NewJSArrayWithElements(
      elements, PACKED_ELEMENTS, allocation));
}

void JsonString::Internalize(Isolate* isolate, AllocationType allocation) {
  if (is_one_byte_) {
    SetObject(isolate->factory()->NewOneByteInternalizedString(bytes_, hash_));
  } else {
    SetObject(isolate->factory()->NewTwoByteInternalizedString(
        Vector<const uint16_t>::cast(bytes_), hash_));
  }
}

void JsonNumber::Internalize(Isolate* isolate, AllocationType allocation) {
  if (is_smi_) {
    SetObject(Handle<Smi>(Smi::FromInt(i_), isolate));
  } else {
    SetObject(isolate->factory()->NewNumber(d_, allocation));
  }
}

template <typename char_type>
void JsonParser2<char_type>::DoParseJson() {
  allocation_ = ((end_ - start_) >= kPretenureTreshold)
                    ? AllocationType::kOld
                    : AllocationType::kYoung;
  result_ = ParseJsonValue();

  SkipWhitespace();
  switch (peek()) {
    case JsonToken::EOS:
      printf("success!\n");
      return;

    case JsonToken::NUMBER:
    case JsonToken::NEGATIVE_NUMBER:
      Report(MessageTemplate::kJsonParseUnexpectedTokenNumber);
      printf("failure!\n");
      return;

    case JsonToken::STRING:
      Report(MessageTemplate::kJsonParseUnexpectedTokenString);
      printf("failure!\n");
      return;

    case JsonToken::LBRACE:
    case JsonToken::LBRACK:
    case JsonToken::RBRACE:
    case JsonToken::RBRACK:
    case JsonToken::TRUE_LITERAL:
    case JsonToken::FALSE_LITERAL:
    case JsonToken::NULL_LITERAL:
    case JsonToken::COLON:
    case JsonToken::COMMA:
    case JsonToken::ILLEGAL:
      Report(MessageTemplate::kJsonParseUnexpectedToken);
      printf("failure!\n");
      return;

    case JsonToken::WHITESPACE:
      UNREACHABLE();
  }
}

template <typename char_type>
JsonString* JsonParser2<char_type>::InternalizeOneByteString(
    const Vector<const uint8_t>& literal) {
  uint32_t hash_field = StringHasher::HashSequentialString<uint8_t>(
      literal.start(), literal.length(), hash_seed_);
  return Internalize(hash_field, true, literal);
}

template <typename char_type>
JsonString* JsonParser2<char_type>::InternalizeTwoByteString(
    const Vector<const uint16_t>& literal) {
  uint32_t hash_field = StringHasher::HashSequentialString<uint16_t>(
      literal.start(), literal.length(), hash_seed_);
  return Internalize(hash_field, false, Vector<const uint8_t>::cast(literal));
}

template <typename char_type>
JsonString* JsonParser2<char_type>::Internalize(
    uint32_t hash, bool is_one_byte, const Vector<const uint8_t>& literal) {
  JsonString string(hash, is_one_byte, literal);
  base::HashMap::Entry* entry = string_table_.LookupOrInsert(&string, hash);
  if (entry->value == nullptr) {
    JsonString* new_string =
        new (zone()) JsonString(zone(), hash, is_one_byte, literal);
    entry->key = new_string;
    entry->value = reinterpret_cast<void*>(1);
    json_values_.AddFront(new_string);
  }
  return static_cast<JsonString*>(entry->key);
}

// Explicit instantiation.
template class JsonParser2<uint8_t>;
template class JsonParser2<uint16_t>;

}  // namespace internal
}  // namespace v8
