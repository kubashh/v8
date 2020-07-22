// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INTL_SUPPORT
#error Internationalization is expected to be enabled.
#endif  // V8_INTL_SUPPORT

#include "src/objects/js-segments.h"

#include <map>
#include <memory>
#include <string>

#include "src/execution/isolate.h"
#include "src/heap/factory.h"
#include "src/objects/intl-objects.h"
#include "src/objects/js-segment-iterator.h"
#include "src/objects/js-segmenter.h"
#include "src/objects/js-segments-inl.h"
#include "src/objects/managed.h"
#include "src/objects/objects-inl.h"
#include "unicode/brkiter.h"

namespace v8 {
namespace internal {

// ecma402 #sec-createsegmentsobject
MaybeHandle<JSSegments> JSSegments::CreateSegmentsObject(
    Isolate* isolate, icu::BreakIterator* break_iterator, Handle<String> string,
    JSSegmenter::Granularity granularity) {
  CHECK_NOT_NULL(break_iterator);

  Handle<Map> map =
      Handle<Map>(isolate->native_context()->intl_segments_map(), isolate);
  Handle<JSObject> result = isolate->factory()->NewJSObjectFromMap(map);

  Handle<Managed<icu::UnicodeString>> unicode_string =
      Intl::SetTextToBreakIterator(isolate, string, break_iterator);
  Handle<Managed<icu::BreakIterator>> managed_break_iterator =
      Managed<icu::BreakIterator>::FromRawPtr(isolate, 0, break_iterator);
  DisallowHeapAllocation no_gc;

  Handle<JSSegments> segments = Handle<JSSegments>::cast(result);
  segments->set_flags(0);
  segments->set_icu_break_iterator(*managed_break_iterator);
  segments->set_unicode_string(*unicode_string);
  segments->set_granularity(granularity);

  return segments;
}

// ecma402 #sec-createsegmentiterator
MaybeHandle<Object> JSSegments::CreateSegmentIterator(
    Isolate* isolate, Handle<JSSegments> segments) {
  Handle<String> string;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, string, GetString(isolate, segments),
                             Object);
  return JSSegmentIterator::Create(
      isolate, segments->icu_break_iterator().raw()->clone(),
      segments->granularity(), string);
}

// ecma402 #sec-%segmentsprototype%.containing
MaybeHandle<Object> JSSegments::Containing(Isolate* isolate,
                                           Handle<JSSegments> segments,
                                           int32_t n) {
  // 5. Let len be the length of string.
  int32_t len = segments->unicode_string().raw()->length();

  // 7. If n < 0 or n ≥ len, return undefined.
  if (n < 0 || n >= len) {
    return isolate->factory()->undefined_value();
  }

  icu::BreakIterator* break_iterator = segments->icu_break_iterator().raw();
  // 8. Let startIndex be ! FindBoundary(segmenter, string, n, before).
  int32_t start_index =
      break_iterator->isBoundary(n) ? n : break_iterator->preceding(n);

  bool granularity_is_word =
      segments->granularity() == JSSegmenter::Granularity::WORD;

  // 9. Let endIndex be ! FindBoundary(segmenter, string, n, after).
  int32_t end_index = break_iterator->following(n);

  // 10. Return ! CreateSegmentDataObject(segmenter, string, startIndex,
  // endIndex).
  return CreateSegmentDataObject(isolate, granularity_is_word, break_iterator,
                                 *(segments->unicode_string().raw()),
                                 start_index, end_index);
}

// ecma402 #sec-createsegmentdataobject
MaybeHandle<Object> JSSegments::CreateSegmentDataObject(
    Isolate* isolate, bool granularity_is_word,
    icu::BreakIterator* break_iterator, const icu::UnicodeString& string,
    int32_t start_index, int32_t end_index) {
  Factory* factory = isolate->factory();
  // 1. Let len be the length of string.
  // 2. Assert: startIndex ≥ 0 and startIndex < len.
  // 3. Assert: endIndex > startIndex and endIndex ≤ len.

  // 4. Let result be ! ObjectCreate(%ObjectPrototype%).
  Handle<JSObject> result = factory->NewJSObject(isolate->object_function());

  // 5. Let segment be the String value containing consecutive code units from
  //    string beginning with the code unit at index startIndex and ending with
  //    the code unit at index endIndex - 1.
  Handle<String> segment;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, segment, Intl::ToString(isolate, string, start_index, end_index),
      JSObject);

  // 6. Perform ! CreateDataPropertyOrThrow(result, "segment", segment).
  CHECK(JSReceiver::CreateDataProperty(isolate, result,
                                       factory->segment_string(), segment,
                                       Just(kDontThrow))
            .FromJust());

  // 7. Perform ! CreateDataPropertyOrThrow(result, "index", startIndex).
  CHECK(JSReceiver::CreateDataProperty(isolate, result, factory->index_string(),
                                       factory->NewNumberFromInt(start_index),
                                       Just(kDontThrow))
            .FromJust());

  Handle<Object> is_word_like;
  // 8. Let granularity be segmenter.[[SegmenterGranularity]].
  // 9. If granularity is "word", then
  if (granularity_is_word) {
    // a. Let isWordLike be a Boolean value indicating whether the word segment
    //    segment in string is "word-like" according to locale
    //    segmenter.[[Locale]].
    int32_t rule_status = break_iterator->getRuleStatus();
    is_word_like =
        ((rule_status >= UBRK_WORD_NUMBER &&
          rule_status < UBRK_WORD_NUMBER_LIMIT) ||
         (rule_status >= UBRK_WORD_LETTER &&
          rule_status < UBRK_WORD_LETTER_LIMIT) ||
         (rule_status >= UBRK_WORD_KANA &&
          rule_status < UBRK_WORD_KANA_LIMIT) ||
         (rule_status >= UBRK_WORD_IDEO && rule_status < UBRK_WORD_IDEO_LIMIT))
            ? factory->true_value()
            : factory->false_value();
  } else {
    // 10. Else
    // a. Let isWordLike be undefined.
    is_word_like = factory->undefined_value();
  }
  // Perform ! CreateDataPropertyOrThrow(result, "isWordLike", isWordLike).
  CHECK(JSReceiver::CreateDataProperty(isolate, result,
                                       factory->isWordLike_string(),
                                       is_word_like, Just(kDontThrow))
            .FromJust());
  return result;
}

MaybeHandle<String> JSSegments::GetString(Isolate* isolate,
                                          Handle<JSSegments> segments) {
  return Intl::ToString(isolate, *(segments->unicode_string().raw()));
}

Handle<String> JSSegments::GranularityAsString(Isolate* isolate) const {
  return JSSegmenter::GetGranularityString(isolate, granularity());
}

}  // namespace internal
}  // namespace v8
