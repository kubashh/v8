// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-iterator-gen.h"
#include "src/builtins/builtins-utils-gen.h"
#include "src/builtins/growable-fixed-array-gen.h"
#include "src/codegen/code-stub-assembler.h"
#include "src/objects/js-temporal-objects-inl.h"
#include "src/objects/objects-inl.h"
#include "src/objects/objects.h"

namespace v8 {
namespace internal {

class TemporalBuiltinsAssembler : public IteratorBuiltinsAssembler {
 public:
  explicit TemporalBuiltinsAssembler(compiler::CodeAssemblerState* state)
      : IteratorBuiltinsAssembler(state) {}

  // Step 3 and later of #sec-temporal.calendar.prototype.fields
  TNode<JSArray> CalendarFieldsArrayFromIterable(
      TNode<Context> context, TNode<JSTemporalCalendar> calendar,
      TNode<Object> iterable);

  // For the use inside Temporal GetPossibleInstantFor
  TNode<FixedArray> TemporalInstantFixedArrayFromIterable(
      TNode<Context> context, TNode<Object> iterable);
};

// Step 3 and later of
// #sec-temporal.calendar.prototype.fields
TNode<JSArray> TemporalBuiltinsAssembler::CalendarFieldsArrayFromIterable(
    TNode<Context> context, TNode<JSTemporalCalendar> calendar,
    TNode<Object> iterable) {
  Label done(this);
  // 4. Let iteratorRecord be ? GetIterator(items).
  IteratorRecord iterator_record = GetIterator(context, iterable);

  // 5. Let fieldNames be a new empty List.
  GrowableFixedArray field_names(state());

  Label loop_start(this, {field_names.var_array(), field_names.var_length(),
                          field_names.var_capacity()});
  Goto(&loop_start);
  // 6. Let next be true.
  // 7. Repeat, while next is not false
  Label if_isnotstringtype(this, Label::kDeferred),
      if_rangeerror(this, Label::kDeferred),
      if_exception(this, Label::kDeferred),
      if_exception2(this, Label::kDeferred), add_fields(this, Label::kDeferred),
      end_of_loop(this, Label::kDeferred);
  BIND(&loop_start);
  {
    //  a. Set next to ? IteratorStep(iteratorRecord).
    TNode<JSReceiver> next =
        IteratorStep(context, iterator_record, &end_of_loop);
    //  b. If next is not false, then
    //   i. Let nextValue be ? IteratorValue(next).
    TNode<Object> next_value = IteratorValue(context, next);
    //   ii. If Type(nextValue) is not String, then
    GotoIf(TaggedIsSmi(next_value), &if_isnotstringtype);
    TNode<Uint16T> next_value_type = LoadInstanceType(CAST(next_value));
    GotoIfNot(IsStringInstanceType(next_value_type), &if_isnotstringtype);

    // Step iii and iv see IsInvalidTemporalCalendarField
    GotoIf(IsTrue(CallRuntime(Runtime::kIsInvalidTemporalCalendarField, context,
                              next_value, field_names.ToFixedArray())),
           &if_rangeerror);

    // v. Append nextValue to the end of the List field_names.
    field_names.Push(next_value);
    Goto(&loop_start);
    // 5.b.ii
    BIND(&if_isnotstringtype);
    {
      // 1. Let error be ThrowCompletion(a newly created TypeError object).
      TVARIABLE(Object, var_exception);
      {
        compiler::ScopedExceptionHandler handler(this, &if_exception,
                                                 &var_exception);
        CallRuntime(Runtime::kThrowTypeError, context,
                    SmiConstant(MessageTemplate::kIterableYieldedNonString),
                    next_value);
      }
      Unreachable();

      // 2. Return ? IteratorClose(iteratorRecord, error).
      BIND(&if_exception);
      {
        IteratorCloseOnException(context, iterator_record);
        CallRuntime(Runtime::kReThrow, context, var_exception.value());
        Unreachable();
      }
    }
  }
  BIND(&if_rangeerror);
  {
    // 1. Let error be ThrowCompletion(a newly created RangeError object).
    TVARIABLE(Object, var_exception2);
    {
      compiler::ScopedExceptionHandler handler(this, &if_exception2,
                                               &var_exception2);
      CallRuntime(Runtime::kThrowRangeError, context,
                  SmiConstant(MessageTemplate::kInvalidTimeValue));
    }
    Unreachable();

    BIND(&if_exception2);
    {
      IteratorCloseOnException(context, iterator_record);
      CallRuntime(Runtime::kReThrow, context, var_exception2.value());
      Unreachable();
    }
  }
  BIND(&end_of_loop);
  {
    const TNode<Uint32T> flags =
        LoadObjectField<Uint32T>(calendar, JSTemporalCalendar::kFlagsOffset);
    const TNode<IntPtrT> index = Signed(
        DecodeWordFromWord32<JSTemporalCalendar::CalendarIndexBits>(flags));
    Branch(IntPtrEqual(index, IntPtrConstant(0)), &done, &add_fields);
    BIND(&add_fields);
    {
      TNode<String> era_string = StringConstant("era");
      field_names.Push(era_string);
      TNode<String> eraYear_string = StringConstant("eraYear");
      field_names.Push(eraYear_string);
    }
    Goto(&done);
  }
  BIND(&done);
  return field_names.ToJSArray(context);
}

// #sec-iterabletolistoftype
TNode<FixedArray>
TemporalBuiltinsAssembler::TemporalInstantFixedArrayFromIterable(
    TNode<Context> context, TNode<Object> iterable) {
  GrowableFixedArray list(state());
  Label done(this);
  // 1. If iterable is undefined, then
  //   a. Return a new empty List.
  GotoIf(IsUndefined(iterable), &done);

  // 2. Let iteratorRecord be ? GetIterator(items).
  IteratorRecord iterator_record = GetIterator(context, iterable);

  // 3. Let list be a new empty List.

  Label loop_start(this,
                   {list.var_array(), list.var_length(), list.var_capacity()});
  Goto(&loop_start);
  // 4. Let next be true.
  // 5. Repeat, while next is not false
  Label if_isnottemporalinstant(this, Label::kDeferred),
      if_exception(this, Label::kDeferred);
  BIND(&loop_start);
  {
    //  a. Set next to ? IteratorStep(iteratorRecord).
    TNode<JSReceiver> next = IteratorStep(context, iterator_record, &done);
    //  b. If next is not false, then
    //   i. Let nextValue be ? IteratorValue(next).
    TNode<Object> next_value = IteratorValue(context, next);
    //   ii. If Type(nextValue) is not Object or nextValue does not have an
    //   [[InitializedTemporalInstant]] internal slot
    GotoIf(TaggedIsSmi(next_value), &if_isnottemporalinstant);
    TNode<Uint16T> next_value_type = LoadInstanceType(CAST(next_value));
    GotoIfNot(IsTemporalInstantInstanceType(next_value_type),
              &if_isnottemporalinstant);
    //   iii. Append nextValue to the end of the List list.
    list.Push(next_value);
    Goto(&loop_start);
    // 5.b.ii
    BIND(&if_isnottemporalinstant);
    {
      // 1. Let error be ThrowCompletion(a newly created TypeError object).
      TVARIABLE(Object, var_exception);
      {
        compiler::ScopedExceptionHandler handler(this, &if_exception,
                                                 &var_exception);
        CallRuntime(Runtime::kThrowTypeError, context,
                    SmiConstant(MessageTemplate::kIterableYieldedNonString),
                    next_value);
      }
      Unreachable();

      // 2. Return ? IteratorClose(iteratorRecord, error).
      BIND(&if_exception);
      IteratorCloseOnException(context, iterator_record);
      CallRuntime(Runtime::kReThrow, context, var_exception.value());
      Unreachable();
    }
  }

  BIND(&done);
  return list.ToFixedArray();
}

TF_BUILTIN(TemporalInstantFixedArrayFromIterable, TemporalBuiltinsAssembler) {
  auto context = Parameter<Context>(Descriptor::kContext);
  auto iterable = Parameter<Object>(Descriptor::kIterable);

  Return(TemporalInstantFixedArrayFromIterable(context, iterable));
}

// #sec-temporal.calendar.prototype.fields
TF_BUILTIN(TemporalCalendarPrototypeFields, TemporalBuiltinsAssembler) {
  auto context = Parameter<Context>(Descriptor::kContext);
  auto argc = UncheckedParameter<Int32T>(Descriptor::kJSActualArgumentsCount);

  CodeStubArguments args(this, argc);

  // 1. Let calendar be this value.
  TNode<Object> receiver = args.GetReceiver();

  // 2. Perform ? RequireInternalSlot(calendar,
  // [[InitializedTemporalCalendar]]).
  ThrowIfNotInstanceType(context, receiver, JS_TEMPORAL_CALENDAR_TYPE,
                         "Temporal.Calendar.prototype.fields");
  TNode<JSTemporalCalendar> calendar = CAST(receiver);

  // Step 3 and later is inside CalendarFieldsArrayFromIterable
  TNode<Object> iterable = args.GetOptionalArgumentValue(0);
  Return(CalendarFieldsArrayFromIterable(context, calendar, iterable));
}

}  // namespace internal
}  // namespace v8
