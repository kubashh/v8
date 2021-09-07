// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-utils-inl.h"
#include "src/builtins/builtins.h"
#include "src/logging/counters.h"
#include "src/objects/elements.h"
#include "src/objects/heap-number-inl.h"
#include "src/objects/js-array-buffer-inl.h"
#include "src/objects/objects-inl.h"

namespace v8 {
namespace internal {

// -----------------------------------------------------------------------------
// ES6 section 22.2 TypedArray Objects

// ES6 section 22.2.3.1 get %TypedArray%.prototype.buffer
BUILTIN(TypedArrayPrototypeBuffer) {
  HandleScope scope(isolate);
  CHECK_RECEIVER(JSTypedArray, typed_array,
                 "get %TypedArray%.prototype.buffer");
  return *typed_array->GetBuffer();
}

namespace {

int64_t CapRelativeIndex(Handle<Object> num, int64_t minimum, int64_t maximum) {
  if (V8_LIKELY(num->IsSmi())) {
    int64_t relative = Smi::ToInt(*num);
    return relative < 0 ? std::max<int64_t>(relative + maximum, minimum)
                        : std::min<int64_t>(relative, maximum);
  } else {
    DCHECK(num->IsHeapNumber());
    double relative = HeapNumber::cast(*num).value();
    DCHECK(!std::isnan(relative));
    return static_cast<int64_t>(
        relative < 0 ? std::max<double>(relative + maximum, minimum)
                     : std::min<double>(relative, maximum));
  }
}

}  // namespace

// https://tc39.es/ecma262/#sec-%typedarray%.prototype.copywithin
BUILTIN(TypedArrayPrototypeCopyWithin) {
  HandleScope scope(isolate);

  // 1. Let O be the this value.
  // 2. Perform ? ValidateTypedArray(O).
  Handle<JSTypedArray> array;
  const char* method = "%TypedArray%.prototype.copyWithin";
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, array, JSTypedArray::Validate(isolate, args.receiver(), method));

  // 3. Let len be O.[[ArrayLength]].
  int64_t len = array->length();
  int64_t to = 0;
  int64_t from = 0;
  int64_t final = len;

  if (V8_LIKELY(args.length() > 1)) {
    // 4. Let relativeTarget be ? ToIntegerOrInfinity(target).
    Handle<Object> num;
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
        isolate, num, Object::ToInteger(isolate, args.at<Object>(1)));
    // 5. If relativeTarget is -∞, let to be 0.
    // 6. Else if relativeTarget < 0, let to be max(len + relativeTarget, 0).
    // 7. Else, let to be min(relativeTarget, len).
    to = CapRelativeIndex(num, 0, len);

    if (args.length() > 2) {
      // 8. Let relativeStart be ? ToIntegerOrInfinity(start).
      ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
          isolate, num, Object::ToInteger(isolate, args.at<Object>(2)));
      // 9. If relativeStart is -∞, let from be 0.
      // 10. Else if relativeStart < 0, let from be max(len + relativeStart, 0).
      // 11. Else, let from be min(relativeStart, len).
      from = CapRelativeIndex(num, 0, len);

      // 12. If end is undefined, let relativeEnd be len; else let relativeEnd
      // be ? ToIntegerOrInfinity(end).
      Handle<Object> end = args.atOrUndefined(isolate, 3);
      if (!end->IsUndefined(isolate)) {
        // 13. If relativeEnd is -∞, let final be 0.
        // 14. Else if relativeEnd < 0, let final be max(len + relativeEnd, 0).
        // 15. Else, let final be min(relativeEnd, len).
        ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, num,
                                           Object::ToInteger(isolate, end));
        final = CapRelativeIndex(num, 0, len);
      }
    }
  }

  // 16. Let count be min(final - from, len - to).
  int64_t count = std::min<int64_t>(final - from, len - to);
  // 17. If count > 0, then
  // Early return here.
  // 18. Return O.
  if (count <= 0) return *array;

  // 17b. Let buffer be O.[[ViewedArrayBuffer]].
  // 17c. If IsDetachedBuffer(buffer) is true, throw a TypeError exception.
  // TypedArray buffer may have been transferred/detached during parameter
  // processing above. Return early in this case, to prevent potential UAF
  // error.
  if (V8_UNLIKELY(array->WasDetached())) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate,
        NewTypeError(MessageTemplate::kDetachedOperation,
                     isolate->factory()->NewStringFromAsciiChecked(method)));
  }

  // Ensure processed indexes are within array bounds
  DCHECK_GE(from, 0);
  DCHECK_LT(from, len);
  DCHECK_GE(to, 0);
  DCHECK_LT(to, len);
  DCHECK_GE(len - count, 0);

  // 17d. Let typedArrayName be the String value of O.[[TypedArrayName]].
  // 17e. Let elementSize be the Element Size value specified in Table 72 for
  // typedArrayName.
  size_t element_size = array->element_size();
  // 17f. Let byteOffset be O.[[ByteOffset]].
  // NOTE: array->DataPtr already shifted by byteOffset at construction.
  // 17g. Let toByteIndex be to × elementSize + byteOffset.
  to = to * element_size;
  // 17h. Let fromByteIndex be from × elementSize + byteOffset.
  from = from * element_size;
  // 17i. Let countBytes be count × elementSize.
  count = count * element_size;

  // 17j. If fromByteIndex < toByteIndex and toByteIndex < fromByteIndex +
  // countBytes, then
  // 17j.i.   Let direction be -1.
  // 17j.ii.  Set fromByteIndex to fromByteIndex + countBytes - 1.
  // 17j.iii. Set toByteIndex to toByteIndex + countBytes - 1.
  // 17k. Else,
  // 17k.i.   Let direction be 1.
  //
  // Overlapping is taken care by both base::Relaxed_Memmove and std::memmove.

  // 17l. Repeat, while countBytes > 0,
  // 17l.i.   Let value be GetValueFromBuffer(buffer, fromByteIndex, Uint8,
  // true, Unordered).
  // 17l.ii.  Perform SetValueInBuffer(buffer, toByteIndex, Uint8, value, true,
  // Unordered).
  // 17l.iii. Set fromByteIndex to fromByteIndex + direction.
  // 17l.iv.  Set toByteIndex to toByteIndex + direction.
  // 17l.v.   Set countBytes to countBytes - 1.
  //
  // All steps defined in 17l are covered by both base::Relaxed_Memmove and
  // std::memmove.
  uint8_t* data = static_cast<uint8_t*>(array->DataPtr());
  if (array->buffer().is_shared()) {
    base::Relaxed_Memmove(reinterpret_cast<base::Atomic8*>(data + to),
                          reinterpret_cast<base::Atomic8*>(data + from), count);
  } else {
    std::memmove(data + to, data + from, count);
  }

  // 18. Return O.
  return *array;
}

BUILTIN(TypedArrayPrototypeFill) {
  HandleScope scope(isolate);

  Handle<JSTypedArray> array;
  const char* method = "%TypedArray%.prototype.fill";
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, array, JSTypedArray::Validate(isolate, args.receiver(), method));
  ElementsKind kind = array->GetElementsKind();

  Handle<Object> obj_value = args.atOrUndefined(isolate, 1);
  if (IsBigIntTypedArrayElementsKind(kind)) {
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, obj_value,
                                       BigInt::FromObject(isolate, obj_value));
  } else {
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, obj_value,
                                       Object::ToNumber(isolate, obj_value));
  }

  int64_t len = array->GetLength();
  int64_t start = 0;
  int64_t end = len;

  if (args.length() > 2) {
    Handle<Object> num = args.atOrUndefined(isolate, 2);
    if (!num->IsUndefined(isolate)) {
      ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
          isolate, num, Object::ToInteger(isolate, num));
      start = CapRelativeIndex(num, 0, len);

      num = args.atOrUndefined(isolate, 3);
      if (!num->IsUndefined(isolate)) {
        ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
            isolate, num, Object::ToInteger(isolate, num));
        end = CapRelativeIndex(num, 0, len);
      }
    }
  }

  if (V8_UNLIKELY(array->IsVariableLength())) {
    bool out_of_bounds = false;
    array->GetLengthOrOutOfBounds(out_of_bounds);
    if (out_of_bounds) {
      const MessageTemplate message = MessageTemplate::kDetachedOperation;
      Handle<String> operation =
          isolate->factory()->NewStringFromAsciiChecked(method);
      THROW_NEW_ERROR_RETURN_FAILURE(isolate, NewTypeError(message, operation));
    }
  } else if (V8_UNLIKELY(array->WasDetached())) {
    return *array;
  }

  int64_t count = end - start;
  if (count <= 0) return *array;

  // Ensure processed indexes are within array bounds
  DCHECK_GE(start, 0);
  DCHECK_LT(start, len);
  DCHECK_GE(end, 0);
  DCHECK_LE(end, len);
  DCHECK_LE(count, len);

  RETURN_RESULT_OR_FAILURE(isolate, ElementsAccessor::ForKind(kind)->Fill(
                                        array, obj_value, start, end));
}

BUILTIN(TypedArrayPrototypeIncludes) {
  HandleScope scope(isolate);

  Handle<JSTypedArray> array;
  const char* method = "%TypedArray%.prototype.includes";
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, array, JSTypedArray::Validate(isolate, args.receiver(), method));

  if (args.length() < 2) return ReadOnlyRoots(isolate).false_value();

  int64_t len = array->length();
  if (len == 0) return ReadOnlyRoots(isolate).false_value();

  int64_t index = 0;
  if (args.length() > 2) {
    Handle<Object> num;
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
        isolate, num, Object::ToInteger(isolate, args.at<Object>(2)));
    index = CapRelativeIndex(num, 0, len);
  }

  // TODO(cwhan.tunz): throw. See the above comment in CopyWithin.
  if (V8_UNLIKELY(array->WasDetached()))
    return ReadOnlyRoots(isolate).false_value();

  Handle<Object> search_element = args.atOrUndefined(isolate, 1);
  ElementsAccessor* elements = array->GetElementsAccessor();
  Maybe<bool> result =
      elements->IncludesValue(isolate, array, search_element, index, len);
  MAYBE_RETURN(result, ReadOnlyRoots(isolate).exception());
  return *isolate->factory()->ToBoolean(result.FromJust());
}

BUILTIN(TypedArrayPrototypeIndexOf) {
  HandleScope scope(isolate);

  Handle<JSTypedArray> array;
  const char* method = "%TypedArray%.prototype.indexOf";
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, array, JSTypedArray::Validate(isolate, args.receiver(), method));

  int64_t len = array->length();
  if (len == 0) return Smi::FromInt(-1);

  int64_t index = 0;
  if (args.length() > 2) {
    Handle<Object> num;
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
        isolate, num, Object::ToInteger(isolate, args.at<Object>(2)));
    index = CapRelativeIndex(num, 0, len);
  }

  // TODO(cwhan.tunz): throw. See the above comment in CopyWithin.
  if (V8_UNLIKELY(array->WasDetached())) return Smi::FromInt(-1);

  Handle<Object> search_element = args.atOrUndefined(isolate, 1);
  ElementsAccessor* elements = array->GetElementsAccessor();
  Maybe<int64_t> result =
      elements->IndexOfValue(isolate, array, search_element, index, len);
  MAYBE_RETURN(result, ReadOnlyRoots(isolate).exception());
  return *isolate->factory()->NewNumberFromInt64(result.FromJust());
}

BUILTIN(TypedArrayPrototypeLastIndexOf) {
  HandleScope scope(isolate);

  Handle<JSTypedArray> array;
  const char* method = "%TypedArray%.prototype.lastIndexOf";
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, array, JSTypedArray::Validate(isolate, args.receiver(), method));

  int64_t len = array->length();
  if (len == 0) return Smi::FromInt(-1);

  int64_t index = len - 1;
  if (args.length() > 2) {
    Handle<Object> num;
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
        isolate, num, Object::ToInteger(isolate, args.at<Object>(2)));
    // Set a negative value (-1) for returning -1 if num is negative and
    // len + num is still negative. Upper bound is len - 1.
    index = std::min<int64_t>(CapRelativeIndex(num, -1, len), len - 1);
  }

  if (index < 0) return Smi::FromInt(-1);

  // TODO(cwhan.tunz): throw. See the above comment in CopyWithin.
  if (V8_UNLIKELY(array->WasDetached())) return Smi::FromInt(-1);

  Handle<Object> search_element = args.atOrUndefined(isolate, 1);
  ElementsAccessor* elements = array->GetElementsAccessor();
  Maybe<int64_t> result =
      elements->LastIndexOfValue(array, search_element, index);
  MAYBE_RETURN(result, ReadOnlyRoots(isolate).exception());
  return *isolate->factory()->NewNumberFromInt64(result.FromJust());
}

BUILTIN(TypedArrayPrototypeReverse) {
  HandleScope scope(isolate);

  Handle<JSTypedArray> array;
  const char* method = "%TypedArray%.prototype.reverse";
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, array, JSTypedArray::Validate(isolate, args.receiver(), method));

  ElementsAccessor* elements = array->GetElementsAccessor();
  elements->Reverse(*array);
  return *array;
}

}  // namespace internal
}  // namespace v8
