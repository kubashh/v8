// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_JSON_PARSER2_H_
#define V8_JSON_PARSER2_H_

#include <stdint.h>
#include <string.h>
#include <vector>

#include "src/base/threaded-list.h"
#include "src/isolate.h"
#include "src/message-template.h"
#include "src/parsing/literal-buffer.h"
#include "src/zone/zone.h"

namespace v8 {
namespace internal {

enum class JsonToken : uint8_t {
  NUMBER,
  NEGATIVE_NUMBER,
  STRING,
  LBRACE,
  RBRACE,
  LBRACK,
  RBRACK,
  TRUE_LITERAL,
  FALSE_LITERAL,
  NULL_LITERAL,
  WHITESPACE,
  COLON,
  COMMA,
  ILLEGAL,
  EOS
};

class JsonValue : public ZoneObject {
 public:
  enum class Kind : uint8_t { STRING, NUMBER, OBJECT, ARRAY, LITERAL };

  explicit JsonValue(Kind kind) : next_(nullptr), kind_(kind) {}

  void* operator new(size_t size, Zone* zone) { return zone->New(size); }

  void Internalize(Isolate* isolate, AllocationType allocation);
  Handle<Object> object() const {
    DCHECK_NOT_NULL(object_);
    DCHECK(is_internalized_);
    return Handle<Object>(object_);
  }

  JsonValue** next() {
    DCHECK(!is_internalized_);
    return &next_;
  }

  Kind kind() const { return kind_; }

 protected:
  void SetObject(Handle<Object> object);

 private:
  // Hidden to prevent accidental usage. It would have to load the
  // current zone from the TLS.
  void* operator new(size_t size);

  union {
    JsonValue* next_;
    Address* object_;
  };

  Kind kind_ : 3;
  bool is_internalized_ = false;
};

class JsonString : public JsonValue {
 public:
  JsonString(Zone* zone, uint32_t hash, bool is_one_byte,
             const Vector<const uint8_t>& bytes)
      : JsonValue(JsonValue::Kind::STRING),
        hash_(hash),
        is_one_byte_(is_one_byte) {
    uint8_t* new_bytes = zone->NewArray<uint8_t>(bytes.length());
    memcpy(new_bytes, bytes.start(), bytes.length());
    bytes_ = Vector<const uint8_t>(new_bytes, bytes.length());
  }

  JsonString(uint32_t hash, bool is_one_byte,
             const Vector<const uint8_t>& bytes)
      : JsonValue(JsonValue::Kind::STRING),
        hash_(hash),
        is_one_byte_(is_one_byte),
        bytes_(bytes) {}

  explicit JsonString(Isolate* isolate)
      : JsonValue(JsonValue::Kind::STRING), is_one_byte_(true) {
    bytes_ = Vector<const uint8_t>(reinterpret_cast<const uint8_t*>(""), 0);
    Handle<String> empty_string = isolate->factory()->empty_string();
    hash_ = empty_string->hash_field();
    SetObject(empty_string);
  }

  bool is_named_property() const {
    return (hash_ & String::kIsNotArrayIndexMask) != 0;
  }

  uint32_t hash() const { return hash_; }

  static bool Compare(void* a, void* b) {
    const JsonString* lhs = static_cast<JsonString*>(a);
    const JsonString* rhs = static_cast<JsonString*>(b);
    if (lhs->is_one_byte_ != lhs->is_one_byte_) return false;
    if (lhs->bytes_.length() != lhs->bytes_.length()) return false;
    return memcmp(lhs->bytes_.start(), rhs->bytes_.start(),
                  lhs->bytes_.length()) == 0;
  }

  void Internalize(Isolate* isolate, AllocationType allocation);

  void Print() { printf("\"%.*s\"", bytes_.length(), bytes_.start()); }

  Handle<String> object() const {
    return Handle<String>::cast(JsonValue::object());
  }

 private:
  // TODO(verwaest): Better packing?
  uint32_t hash_;
  bool is_one_byte_;
  Vector<const uint8_t> bytes_;
};

class JsonNumber : public JsonValue {
 public:
  void Internalize(Isolate* isolate, AllocationType allocation);
};

class JsonObject;

class JsonMap : public ZoneObject {
 public:
  JsonMap(JsonMap* parent, JsonString* property)
      : parent_(parent),
        property_(property),
        number_of_properties_(parent->number_of_properties_ + 1),
        hash_(parent->hash() ^ property->hash()) {
    parent->set_transition(this);
  }

  JsonMap()
      : parent_(nullptr),
        property_(nullptr),
        number_of_properties_(0),
        hash_(0) {}

  uint32_t hash() const { return hash_; }

  JsonString* property() const { return property_; }
  JsonMap* field_type() const { return field_type_; }

  JsonString* expected_transition_property() const {
    return transition_ == nullptr ? nullptr : transition_->property_;
  }

  JsonMap* expected_transition() const { return transition_; }

  void UpdateFieldType(JsonValue* value);

  static bool Compare(void* a, void* b) {
    const JsonMap* lhs = static_cast<JsonMap*>(a);
    const JsonMap* rhs = static_cast<JsonMap*>(b);
    return lhs->parent_ == rhs->parent_ && lhs->property_ == rhs->property_;
  }

  Handle<Map> Internalize(Isolate* isolate, AllocationType allocation);

 private:
  void set_transition(JsonMap* transition) {
    transition_ = seen_transition_ ? nullptr : transition;
    seen_transition_ = true;
  }

  // TODO(verwaest): Union?
  Handle<Map> object_;
  JsonMap* parent_;
  JsonString* property_;
  // TODO(verwaest): Pack?
  JsonMap* field_type_;
  JsonMap* transition_ = nullptr;
  bool seen_field_type_ = false;
  bool seen_transition_ = false;
  uint32_t number_of_properties_;
  uint32_t hash_;
};

class JsonObject : public JsonValue {
 public:
  explicit JsonObject(Zone* zone, JsonMap* map,
                      const ScopedPtrList<JsonValue>& properties,
                      const ScopedPtrList<JsonValue>& elements)
      : JsonValue(JsonValue::Kind::OBJECT),
        map_(map),
        properties_(0, zone),
        elements_(0, zone) {
    properties.CopyTo(&properties_, zone);
    elements.CopyTo(&elements_, zone);
  }

  void Internalize(Isolate* isolate, AllocationType allocation);

  JsonMap* map() const { return map_; }

 private:
  JsonMap* map_;
  ZonePtrList<JsonValue> properties_;
  ZonePtrList<JsonValue> elements_;
};

class JsonArray : public JsonValue {
 public:
  JsonArray(Zone* zone, const ScopedPtrList<JsonValue>& elements)
      : JsonValue(JsonValue::Kind::ARRAY), elements_(0, zone) {
    elements.CopyTo(&elements_, zone);
  }

  void Internalize(Isolate* isolate, AllocationType allocation);

 private:
  ZonePtrList<JsonValue> elements_;
};

class JsonLiteral : public JsonValue {
 public:
  explicit JsonLiteral(const char* name, Handle<Object> literal)
      : JsonValue(JsonValue::Kind::LITERAL), name_(name) {
    SetObject(literal);
  }

  void* operator new(size_t size) { return ::operator new(size); }

  void Print() { printf("%s", name_); }

 private:
  const char* name_;
};

// Json parser.
template <typename char_type>
class JsonParser2 {
 public:
  explicit JsonParser2(Isolate* isolate);

  void ParseJson(Handle<String> input);
  Handle<Object> InternalizeJson(Isolate* isolate);

 private:
  void DoParseJson();
  JsonValue* ParseJsonValue();
  JsonObject* ParseObject();
  JsonArray* ParseArray();

  void InternalizeJsonObject(Isolate* isolate, JsonObject* object);
  void InternalizeJsonArray(Isolate* isolate, JsonArray* array);
  void InternalizeJsonString(Isolate* isolate, JsonString* string);
  void InternalizeJsonNumber(Isolate* isolate, JsonNumber* number);

  void Report(MessageTemplate message_template) {
    if (cursor_ > end_) return;
    cursor_ = end_ + 1;
    message_template_ = message_template;
  }

  JsonToken peek() const { return next_; }

  JsonNumber* ScanNumber(int sign);
  JsonString* ScanString();

  template <size_t N>
  void ScanLiteral(const char (&s)[N]) {
    DCHECK(!is_at_end());
    if (V8_UNLIKELY(static_cast<size_t>(end_ - cursor_) < N - 2)) {
      Report(MessageTemplate::kJsonParseUnexpectedEOS);
      return;
    }
    const char* i = s;
    int todo = N - 1;
    while (--todo) {
      if (V8_UNLIKELY(*++i != *++cursor_)) {
        Report(MessageTemplate::kJsonParseUnexpectedToken);
        return;
      }
    }
    ++cursor_;
    return;
  }

  JsonMap* Transition(JsonMap* parent, JsonString* property);

  void advance() { ++cursor_; }

  char_type NextCharacter() {
    advance();
    if (is_at_end()) return -1;
    return *cursor_;
  }

  void Consume(JsonToken token) {
    DCHECK_EQ(peek(), token);
    advance();
  }

  void Expect(JsonToken token) {
    if (V8_UNLIKELY(peek() != token)) {
      Report(MessageTemplate::kJsonParseUnexpectedToken);
    }
    advance();
  }

  void ExpectNext(JsonToken token) {
    SkipWhitespace();
    Expect(token);
  }

  bool Check(JsonToken token) {
    SkipWhitespace();
    if (next_ != token) return false;
    advance();
    return true;
  }

  void SkipWhitespace();

  Zone* zone() { return &zone_; }

  bool is_at_end() const { return end_ <= cursor_; }

  void AddLiteralChar(uc32 c) { literal_buffer_.AddChar(c); }

  JsonString* InternalizeOneByteString(const Vector<const uint8_t>& literal);
  JsonString* InternalizeTwoByteString(const Vector<const uint16_t>& literal);
  JsonString* Internalize(uint32_t hash, bool is_one_byte,
                          const Vector<const uint8_t>& literal);

  char_type* start_;
  char_type* cursor_;
  char_type* end_;
  JsonToken next_ = JsonToken::ILLEGAL;
  MessageTemplate message_template_ = MessageTemplate::kNone;

  static const bool kIsOneByte = sizeof(char_type) == 1;
  static const int kDefaultBufferSize_ = 32;
  std::vector<void*> property_buffer_;
  std::vector<void*> element_buffer_;

  bool is_one_byte_literal_ = true;
  LiteralBuffer literal_buffer_;
  Zone zone_;
  uint64_t hash_seed_;

  base::CustomMatcherHashMap string_table_;
  base::CustomMatcherHashMap transition_table_;

  JsonLiteral null_;
  JsonLiteral true_;
  JsonLiteral false_;
  JsonString empty_string_;
  JsonMap root_map_;

  JsonValue* result_ = nullptr;

  base::ThreadedList<JsonValue> json_values_;

  static const int kPretenureTreshold = 100 * 1024;
  AllocationType allocation_;
};

template <>
inline void JsonParser2<uint8_t>::ParseJson(Handle<String> input) {
  DisallowHeapAllocation no_allocation;

  start_ = SeqOneByteString::cast(*input)->GetChars(no_allocation);
  cursor_ = start_;
  end_ = start_ + input->length();

  DoParseJson();
}

template <>
inline void JsonParser2<uint16_t>::ParseJson(Handle<String> input) {
  DisallowHeapAllocation no_allocation;

  start_ = SeqTwoByteString::cast(*input)->GetChars(no_allocation);
  cursor_ = start_;
  end_ = start_ + input->length();

  DoParseJson();
}

// Explicit instantiation declarations.
extern template class JsonParser2<uint8_t>;
extern template class JsonParser2<uint16_t>;

}  // namespace internal
}  // namespace v8

#endif  // V8_JSON_PARSER2_H_
