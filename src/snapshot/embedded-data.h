// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SNAPSHOT_EMBEDDED_DATA_H_
#define V8_SNAPSHOT_EMBEDDED_DATA_H_

#include "src/base/macros.h"
#include "src/builtins/builtins.h"
#include "src/globals.h"

namespace v8 {
namespace internal {

class Code;
class Isolate;

// Wraps an off-heap instruction stream.
// TODO(jgruber,v8:6666): Remove this class.
class InstructionStream final : public AllStatic {
 public:
  // Returns true, iff the given pc points into an off-heap instruction stream.
  static bool PcIsOffHeap(Isolate* isolate, Address pc);

  // Returns the corresponding Code object if it exists, and nullptr otherwise.
  static Code* TryLookupCode(Isolate* isolate, Address address);

  // During snapshot creation, we first create an executable off-heap area
  // containing all off-heap code. The area is guaranteed to be contiguous.
  // Note that this only applies when building the snapshot, e.g. for
  // mksnapshot. Otherwise, off-heap code is embedded directly into the binary.
  static void CreateOffHeapInstructionStream(Isolate* isolate, uint8_t** data,
                                             uint32_t* size);
  static void FreeOffHeapInstructionStream(uint8_t* data, uint32_t size);
};

class EmbeddedData final {
 public:
  static EmbeddedData FromIsolate(Isolate* isolate);
  static EmbeddedData FromBlob();

  const uint8_t* data() const { return data_; }
  uint32_t size() const { return size_; }

  void Dispose() { delete[] data_; }

  Address InstructionStartOfBuiltin(int i) const;
  uint32_t InstructionSizeOfBuiltin(int i) const;

  bool ContainsBuiltin(int i) const { return InstructionSizeOfBuiltin(i) > 0; }

  // Padded with kCodeAlignment.
  uint32_t PaddedInstructionSizeOfBuiltin(int i) const {
    uint32_t size = InstructionSizeOfBuiltin(i);
    return (size == 0) ? 0 : PadAndAlign(size);
  }

  size_t CreateHash() const;
  size_t Hash() const {
    return *reinterpret_cast<const size_t*>(data_ + HashOffset());
  }

  struct Metadata {
    // Blob layout information.
    uint32_t instructions_offset;
    uint32_t instructions_length;
  };
  STATIC_ASSERT(offsetof(Metadata, instructions_offset) == 0);
  STATIC_ASSERT(offsetof(Metadata, instructions_length) == kUInt32Size);
  STATIC_ASSERT(sizeof(Metadata) == kUInt32Size + kUInt32Size);

  static constexpr int kBuiltinCount = Builtins::builtin_count;

  // The layout of the blob is as follows:
  //
  // [0] hash of the remaining blob
  // [1] metadata of instruction stream 0
  // ... metadata
  // ... instruction streams

  static constexpr uint32_t kTableSize = static_cast<uint32_t>(kBuiltinCount);
  static constexpr uint32_t HashOffset() { return 0; }
  static constexpr uint32_t HashSize() { return kSizetSize; }
  static constexpr uint32_t MetadataOffset() {
    return HashOffset() + HashSize();
  }
  static constexpr uint32_t MetadataSize() {
    return sizeof(struct Metadata) * kTableSize;
  }
  static constexpr uint32_t RawDataOffset() {
    return PadAndAlign(MetadataOffset() + MetadataSize());
  }

 private:
  EmbeddedData(const uint8_t* data, uint32_t size) : data_(data), size_(size) {}

  const Metadata* Metadata() const {
    return reinterpret_cast<const struct Metadata*>(data_ + MetadataOffset());
  }
  const uint8_t* RawData() const { return data_ + RawDataOffset(); }

  static constexpr int PadAndAlign(int size) {
    // Ensure we have at least one byte trailing the actual builtin
    // instructions which we can later fill with int3.
    return RoundUp<kCodeAlignment>(size + 1);
  }

  void PrintStatistics() const;

  const uint8_t* data_;
  uint32_t size_;
};

constexpr int kIndexMap[] = {
    Builtins::kStackCheckHandler,
    Builtins::kCreateClosureHandler,
    Builtins::kStarHandler,
    Builtins::kReturnHandler,
    Builtins::kLdaUndefinedHandler,
    Builtins::kCallNoFeedbackHandler,
    Builtins::kCreateFunctionContextHandler,
    Builtins::kPushContextHandler,
    Builtins::kStaCurrentContextSlotHandler,
    Builtins::kLdaGlobalHandler,
    Builtins::kLdaConstantHandler,
    Builtins::kLdaSmiHandler,
    Builtins::kCallRuntimeHandler,
    Builtins::kLdaZeroHandler,
    Builtins::kStaInArrayLiteralHandler,
    Builtins::kLdarHandler,
    Builtins::kLdaImmutableCurrentContextSlotHandler,
    Builtins::kLdaNamedPropertyNoFeedbackHandler,
    Builtins::kStaNamedOwnPropertyHandler,
    Builtins::kStaNamedPropertyHandler,
    Builtins::kLdaNamedPropertyHandler,
    Builtins::kLdaFalseHandler,
    Builtins::kMovHandler,
    Builtins::kCallUndefinedReceiverHandler,
    Builtins::kJumpIfToBooleanFalseHandler,
    Builtins::kLdaTrueHandler,
    Builtins::kCallUndefinedReceiver2Handler,
    Builtins::kCallProperty1Handler,
    Builtins::kLdaImmutableContextSlotHandler,
    Builtins::kLdaKeyedPropertyHandler,
    Builtins::kStaKeyedPropertyHandler,
    Builtins::kJumpIfToBooleanTrueHandler,
    Builtins::kCreateEmptyObjectLiteralHandler,
    Builtins::kStaGlobalHandler,
    Builtins::kStaNamedPropertyNoFeedbackHandler,
    Builtins::kCallUndefinedReceiver0Handler,
    Builtins::kCallUndefinedReceiver1Handler,
    Builtins::kAddHandler,
    Builtins::kCreateArrayLiteralHandler,
    Builtins::kCallPropertyHandler,
    Builtins::kLdaTheHoleHandler,
    Builtins::kCreateRegExpLiteralHandler,
    Builtins::kTestEqualHandler,
    Builtins::kJumpIfFalseHandler,
    Builtins::kCallProperty0Handler,
    Builtins::kJumpIfJSReceiverHandler,
    Builtins::kInvokeIntrinsicHandler,
    Builtins::kToBooleanLogicalNotHandler,
    Builtins::kJumpLoopHandler,
    Builtins::kJumpHandler,
    Builtins::kCreateCatchContextHandler,
    Builtins::kTestEqualStrictHandler,
    Builtins::kPopContextHandler,
    Builtins::kSetPendingMessageHandler,
    Builtins::kJumpIfTrueHandler,
    Builtins::kTestUndetectableHandler,
    Builtins::kTestTypeOfHandler,
    Builtins::kThrowHandler,
    Builtins::kTestReferenceEqualHandler,
    Builtins::kReThrowHandler,
    Builtins::kCallProperty2Handler,
    Builtins::kCreateUnmappedArgumentsHandler,
    Builtins::kConstructHandler,
    Builtins::kLdaGlobalInsideTypeofHandler,
    Builtins::kJumpIfUndefinedHandler,
    Builtins::kJumpIfNullHandler,
    Builtins::kToObjectHandler,
    Builtins::kForInEnumerateHandler,
    Builtins::kForInPrepareHandler,
    Builtins::kForInContinueHandler,
    Builtins::kForInNextHandler,
    Builtins::kForInStepHandler,
    Builtins::kCreateObjectLiteralHandler,
    Builtins::kTestLessThanHandler,
    Builtins::kLdaNullHandler,
    Builtins::kLdaCurrentContextSlotHandler,
    Builtins::kThrowReferenceErrorIfHoleHandler,
    Builtins::kCallRuntimeForPairHandler,
    Builtins::kCallAnyReceiverHandler,
    Builtins::kTestGreaterThanHandler,
    Builtins::kJumpIfFalseConstantHandler,
    Builtins::kIncHandler,
    Builtins::kMulHandler,
    Builtins::kCreateEmptyArrayLiteralHandler,
    Builtins::kJumpConstantHandler,
    Builtins::kLogicalNotHandler,
    Builtins::kTypeOfHandler,
    Builtins::kTestInstanceOfHandler,
    Builtins::kSubHandler,
    Builtins::kToNumericHandler,
    Builtins::kMulSmiHandler,
    Builtins::kDivHandler,
    Builtins::kTestGreaterThanOrEqualHandler,
    Builtins::kToStringHandler,
    Builtins::kLdaContextSlotHandler,
    Builtins::kTestInHandler,
    Builtins::kTestUndefinedHandler,
    Builtins::kDeletePropertyStrictHandler,
    Builtins::kTestLessThanOrEqualHandler,
    Builtins::kBitwiseOrHandler,
    Builtins::kStaContextSlotHandler,
    Builtins::kJumpIfToBooleanTrueConstantHandler,
    Builtins::kJumpIfUndefinedConstantHandler,
    Builtins::kJumpIfNullConstantHandler,
    Builtins::kBitwiseAndHandler,
    Builtins::kJumpIfToBooleanFalseConstantHandler,
    Builtins::kJumpIfTrueConstantHandler,
    Builtins::kTestNullHandler,
    Builtins::kJumpIfNotUndefinedHandler,
    Builtins::kSubSmiHandler,
    Builtins::kAddSmiHandler,
    Builtins::kDecHandler,
    Builtins::kBitwiseNotHandler,
    Builtins::kNegateHandler,
    Builtins::kJumpIfNotNullHandler,
    Builtins::kBitwiseOrSmiHandler,
    Builtins::kBitwiseAndSmiHandler,
    Builtins::kSwitchOnSmiNoFeedbackHandler,
    Builtins::kToNumberHandler,
    Builtins::kDeletePropertySloppyHandler,
    Builtins::kShiftLeftHandler,
    Builtins::kBitwiseXorHandler,
    Builtins::kCreateBlockContextHandler,
    Builtins::kCreateMappedArgumentsHandler,
    Builtins::kBitwiseXorSmiHandler,
    Builtins::kCreateWithContextHandler,
    Builtins::kLdaLookupSlotHandler,
    Builtins::kStaLookupSlotHandler,
    Builtins::kDivSmiHandler,
    Builtins::kModSmiHandler,
    Builtins::kRecordWrite,
    Builtins::kAdaptorWithExitFrame,
    Builtins::kAdaptorWithBuiltinExitFrame,
    Builtins::kArgumentsAdaptorTrampoline,
    Builtins::kCallFunction_ReceiverIsNullOrUndefined,
    Builtins::kCallFunction_ReceiverIsNotNullOrUndefined,
    Builtins::kCallFunction_ReceiverIsAny,
    Builtins::kCallBoundFunction,
    Builtins::kCall_ReceiverIsNullOrUndefined,
    Builtins::kCall_ReceiverIsNotNullOrUndefined,
    Builtins::kCall_ReceiverIsAny,
    Builtins::kCallProxy,
    Builtins::kCallVarargs,
    Builtins::kCallWithSpread,
    Builtins::kCallWithArrayLike,
    Builtins::kCallForwardVarargs,
    Builtins::kCallFunctionForwardVarargs,
    Builtins::kConstructFunction,
    Builtins::kConstructBoundFunction,
    Builtins::kConstructedNonConstructable,
    Builtins::kConstruct,
    Builtins::kConstructVarargs,
    Builtins::kConstructWithSpread,
    Builtins::kConstructWithArrayLike,
    Builtins::kConstructForwardVarargs,
    Builtins::kConstructFunctionForwardVarargs,
    Builtins::kJSConstructStubGeneric,
    Builtins::kJSBuiltinsConstructStub,
    Builtins::kFastNewObject,
    Builtins::kFastNewClosure,
    Builtins::kFastNewFunctionContextEval,
    Builtins::kFastNewFunctionContextFunction,
    Builtins::kCreateRegExpLiteral,
    Builtins::kCreateEmptyArrayLiteral,
    Builtins::kCreateShallowArrayLiteral,
    Builtins::kCreateShallowObjectLiteral,
    Builtins::kConstructProxy,
    Builtins::kJSEntryTrampoline,
    Builtins::kJSConstructEntryTrampoline,
    Builtins::kResumeGeneratorTrampoline,
    Builtins::kInterruptCheck,
    Builtins::kStackCheck,
    Builtins::kStringCharAt,
    Builtins::kStringCodePointAtUTF16,
    Builtins::kStringCodePointAtUTF32,
    Builtins::kStringEqual,
    Builtins::kStringGreaterThan,
    Builtins::kStringGreaterThanOrEqual,
    Builtins::kStringIndexOf,
    Builtins::kStringLessThan,
    Builtins::kStringLessThanOrEqual,
    Builtins::kStringRepeat,
    Builtins::kStringSubstring,
    Builtins::kOrderedHashTableHealIndex,
    Builtins::kInterpreterEntryTrampoline,
    Builtins::kInterpreterPushArgsThenCall,
    Builtins::kInterpreterPushUndefinedAndArgsThenCall,
    Builtins::kInterpreterPushArgsThenCallWithFinalSpread,
    Builtins::kInterpreterPushArgsThenConstruct,
    Builtins::kInterpreterPushArgsThenConstructArrayFunction,
    Builtins::kInterpreterPushArgsThenConstructWithFinalSpread,
    Builtins::kInterpreterEnterBytecodeAdvance,
    Builtins::kInterpreterEnterBytecodeDispatch,
    Builtins::kInterpreterOnStackReplacement,
    Builtins::kCompileLazy,
    Builtins::kCompileLazyDeoptimizedCode,
    Builtins::kInstantiateAsmJs,
    Builtins::kNotifyDeoptimized,
    Builtins::kContinueToCodeStubBuiltin,
    Builtins::kContinueToCodeStubBuiltinWithResult,
    Builtins::kContinueToJavaScriptBuiltin,
    Builtins::kContinueToJavaScriptBuiltinWithResult,
    Builtins::kHandleApiCall,
    Builtins::kHandleApiCallAsFunction,
    Builtins::kHandleApiCallAsConstructor,
    Builtins::kAllocateInNewSpace,
    Builtins::kAllocateInOldSpace,
    Builtins::kCopyFastSmiOrObjectElements,
    Builtins::kGrowFastDoubleElements,
    Builtins::kGrowFastSmiOrObjectElements,
    Builtins::kNewArgumentsElements,
    Builtins::kDebugBreakTrampoline,
    Builtins::kFrameDropperTrampoline,
    Builtins::kHandleDebuggerStatement,
    Builtins::kToObject,
    Builtins::kToBoolean,
    Builtins::kOrdinaryToPrimitive_Number,
    Builtins::kOrdinaryToPrimitive_String,
    Builtins::kNonPrimitiveToPrimitive_Default,
    Builtins::kNonPrimitiveToPrimitive_Number,
    Builtins::kNonPrimitiveToPrimitive_String,
    Builtins::kStringToNumber,
    Builtins::kToName,
    Builtins::kNonNumberToNumber,
    Builtins::kNonNumberToNumeric,
    Builtins::kToNumber,
    Builtins::kToNumberConvertBigInt,
    Builtins::kToNumeric,
    Builtins::kNumberToString,
    Builtins::kToString,
    Builtins::kToInteger,
    Builtins::kToInteger_TruncateMinusZero,
    Builtins::kToLength,
    Builtins::kTypeof,
    Builtins::kGetSuperConstructor,
    Builtins::kToBooleanLazyDeoptContinuation,
    Builtins::kKeyedLoadIC_PolymorphicName,
    Builtins::kKeyedLoadIC_Slow,
    Builtins::kKeyedStoreIC_Megamorphic,
    Builtins::kKeyedStoreIC_Slow,
    Builtins::kLoadGlobalIC_Slow,
    Builtins::kLoadIC_FunctionPrototype,
    Builtins::kLoadIC_Slow,
    Builtins::kLoadIC_StringLength,
    Builtins::kLoadIC_StringWrapperLength,
    Builtins::kLoadIC_Uninitialized,
    Builtins::kStoreGlobalIC_Slow,
    Builtins::kStoreIC_Uninitialized,
    Builtins::kStoreInArrayLiteralIC_Slow,
    Builtins::kEnqueueMicrotask,
    Builtins::kRunMicrotasks,
    Builtins::kHasProperty,
    Builtins::kDeleteProperty,
    Builtins::kAbort,
    Builtins::kAbortJS,
    Builtins::kEmptyFunction,
    Builtins::kIllegal,
    Builtins::kStrictPoisonPillThrower,
    Builtins::kUnsupportedThrower,
    Builtins::kReturnReceiver,
    Builtins::kArrayConstructor,
    Builtins::kArrayConstructorImpl,
    Builtins::kArrayNoArgumentConstructor_PackedSmi_DontOverride,
    Builtins::kArrayNoArgumentConstructor_HoleySmi_DontOverride,
    Builtins::kArrayNoArgumentConstructor_PackedSmi_DisableAllocationSites,
    Builtins::kArrayNoArgumentConstructor_HoleySmi_DisableAllocationSites,
    Builtins::kArrayNoArgumentConstructor_Packed_DisableAllocationSites,
    Builtins::kArrayNoArgumentConstructor_Holey_DisableAllocationSites,
    Builtins::kArrayNoArgumentConstructor_PackedDouble_DisableAllocationSites,
    Builtins::kArrayNoArgumentConstructor_HoleyDouble_DisableAllocationSites,
    Builtins::kArraySingleArgumentConstructor_PackedSmi_DontOverride,
    Builtins::kArraySingleArgumentConstructor_HoleySmi_DontOverride,
    Builtins::kArraySingleArgumentConstructor_PackedSmi_DisableAllocationSites,
    Builtins::kArraySingleArgumentConstructor_HoleySmi_DisableAllocationSites,
    Builtins::kArraySingleArgumentConstructor_Packed_DisableAllocationSites,
    Builtins::kArraySingleArgumentConstructor_Holey_DisableAllocationSites,
    Builtins::kArraySingleArgumentConstructor_PackedDouble_DisableAllocationSites,
    Builtins::kArraySingleArgumentConstructor_HoleyDouble_DisableAllocationSites,
    Builtins::kArrayNArgumentsConstructor,
    Builtins::kInternalArrayConstructor,
    Builtins::kInternalArrayConstructorImpl,
    Builtins::kInternalArrayNoArgumentConstructor_Packed,
    Builtins::kInternalArrayNoArgumentConstructor_Holey,
    Builtins::kInternalArraySingleArgumentConstructor_Packed,
    Builtins::kInternalArraySingleArgumentConstructor_Holey,
    Builtins::kArrayConcat,
    Builtins::kArrayIsArray,
    Builtins::kArrayPrototypeFill,
    Builtins::kArrayFrom,
    Builtins::kArrayIncludesSmiOrObject,
    Builtins::kArrayIncludesPackedDoubles,
    Builtins::kArrayIncludesHoleyDoubles,
    Builtins::kArrayIncludes,
    Builtins::kArrayIndexOfSmiOrObject,
    Builtins::kArrayIndexOfPackedDoubles,
    Builtins::kArrayIndexOfHoleyDoubles,
    Builtins::kArrayIndexOf,
    Builtins::kArrayPop,
    Builtins::kArrayPrototypePop,
    Builtins::kArrayPush,
    Builtins::kArrayPrototypePush,
    Builtins::kArrayShift,
    Builtins::kArrayPrototypeShift,
    Builtins::kArrayPrototypeSlice,
    Builtins::kArrayUnshift,
    Builtins::kCloneFastJSArray,
    Builtins::kCloneFastJSArrayFillingHoles,
    Builtins::kExtractFastJSArray,
    Builtins::kArrayEveryLoopContinuation,
    Builtins::kArrayEveryLoopEagerDeoptContinuation,
    Builtins::kArrayEveryLoopLazyDeoptContinuation,
    Builtins::kArrayEvery,
    Builtins::kArraySomeLoopContinuation,
    Builtins::kArraySomeLoopEagerDeoptContinuation,
    Builtins::kArraySomeLoopLazyDeoptContinuation,
    Builtins::kArraySome,
    Builtins::kArrayFilterLoopContinuation,
    Builtins::kArrayFilter,
    Builtins::kArrayFilterLoopEagerDeoptContinuation,
    Builtins::kArrayFilterLoopLazyDeoptContinuation,
    Builtins::kArrayMapLoopContinuation,
    Builtins::kArrayMapLoopEagerDeoptContinuation,
    Builtins::kArrayMapLoopLazyDeoptContinuation,
    Builtins::kArrayMap,
    Builtins::kArrayReduceLoopContinuation,
    Builtins::kArrayReducePreLoopEagerDeoptContinuation,
    Builtins::kArrayReduceLoopEagerDeoptContinuation,
    Builtins::kArrayReduceLoopLazyDeoptContinuation,
    Builtins::kArrayReduce,
    Builtins::kArrayReduceRightLoopContinuation,
    Builtins::kArrayReduceRightPreLoopEagerDeoptContinuation,
    Builtins::kArrayReduceRightLoopEagerDeoptContinuation,
    Builtins::kArrayReduceRightLoopLazyDeoptContinuation,
    Builtins::kArrayReduceRight,
    Builtins::kArrayPrototypeEntries,
    Builtins::kArrayFindLoopContinuation,
    Builtins::kArrayFindLoopEagerDeoptContinuation,
    Builtins::kArrayFindLoopLazyDeoptContinuation,
    Builtins::kArrayFindLoopAfterCallbackLazyDeoptContinuation,
    Builtins::kArrayPrototypeFind,
    Builtins::kArrayFindIndexLoopContinuation,
    Builtins::kArrayFindIndexLoopEagerDeoptContinuation,
    Builtins::kArrayFindIndexLoopLazyDeoptContinuation,
    Builtins::kArrayFindIndexLoopAfterCallbackLazyDeoptContinuation,
    Builtins::kArrayPrototypeFindIndex,
    Builtins::kArrayPrototypeKeys,
    Builtins::kArrayPrototypeValues,
    Builtins::kArrayIteratorPrototypeNext,
    Builtins::kFlattenIntoArray,
    Builtins::kFlatMapIntoArray,
    Builtins::kArrayPrototypeFlat,
    Builtins::kArrayPrototypeFlatMap,
    Builtins::kArrayBufferConstructor,
    Builtins::kArrayBufferConstructor_DoNotInitialize,
    Builtins::kArrayBufferPrototypeGetByteLength,
    Builtins::kArrayBufferIsView,
    Builtins::kArrayBufferPrototypeSlice,
    Builtins::kAsyncFunctionEnter,
    Builtins::kAsyncFunctionReject,
    Builtins::kAsyncFunctionResolve,
    Builtins::kAsyncFunctionLazyDeoptContinuation,
    Builtins::kAsyncFunctionAwaitCaught,
    Builtins::kAsyncFunctionAwaitUncaught,
    Builtins::kAsyncFunctionAwaitRejectClosure,
    Builtins::kAsyncFunctionAwaitResolveClosure,
    Builtins::kBigIntConstructor,
    Builtins::kBigIntAsUintN,
    Builtins::kBigIntAsIntN,
    Builtins::kBigIntPrototypeToLocaleString,
    Builtins::kBigIntPrototypeToString,
    Builtins::kBigIntPrototypeValueOf,
    Builtins::kBooleanConstructor,
    Builtins::kBooleanPrototypeToString,
    Builtins::kBooleanPrototypeValueOf,
    Builtins::kCallSitePrototypeGetColumnNumber,
    Builtins::kCallSitePrototypeGetEvalOrigin,
    Builtins::kCallSitePrototypeGetFileName,
    Builtins::kCallSitePrototypeGetFunction,
    Builtins::kCallSitePrototypeGetFunctionName,
    Builtins::kCallSitePrototypeGetLineNumber,
    Builtins::kCallSitePrototypeGetMethodName,
    Builtins::kCallSitePrototypeGetPosition,
    Builtins::kCallSitePrototypeGetPromiseIndex,
    Builtins::kCallSitePrototypeGetScriptNameOrSourceURL,
    Builtins::kCallSitePrototypeGetThis,
    Builtins::kCallSitePrototypeGetTypeName,
    Builtins::kCallSitePrototypeIsAsync,
    Builtins::kCallSitePrototypeIsConstructor,
    Builtins::kCallSitePrototypeIsEval,
    Builtins::kCallSitePrototypeIsNative,
    Builtins::kCallSitePrototypeIsPromiseAll,
    Builtins::kCallSitePrototypeIsToplevel,
    Builtins::kCallSitePrototypeToString,
    Builtins::kConsoleDebug,
    Builtins::kConsoleError,
    Builtins::kConsoleInfo,
    Builtins::kConsoleLog,
    Builtins::kConsoleWarn,
    Builtins::kConsoleDir,
    Builtins::kConsoleDirXml,
    Builtins::kConsoleTable,
    Builtins::kConsoleTrace,
    Builtins::kConsoleGroup,
    Builtins::kConsoleGroupCollapsed,
    Builtins::kConsoleGroupEnd,
    Builtins::kConsoleClear,
    Builtins::kConsoleCount,
    Builtins::kConsoleCountReset,
    Builtins::kConsoleAssert,
    Builtins::kFastConsoleAssert,
    Builtins::kConsoleProfile,
    Builtins::kConsoleProfileEnd,
    Builtins::kConsoleTime,
    Builtins::kConsoleTimeLog,
    Builtins::kConsoleTimeEnd,
    Builtins::kConsoleTimeStamp,
    Builtins::kConsoleContext,
    Builtins::kDataViewConstructor,
    Builtins::kDateConstructor,
    Builtins::kDatePrototypeGetDate,
    Builtins::kDatePrototypeGetDay,
    Builtins::kDatePrototypeGetFullYear,
    Builtins::kDatePrototypeGetHours,
    Builtins::kDatePrototypeGetMilliseconds,
    Builtins::kDatePrototypeGetMinutes,
    Builtins::kDatePrototypeGetMonth,
    Builtins::kDatePrototypeGetSeconds,
    Builtins::kDatePrototypeGetTime,
    Builtins::kDatePrototypeGetTimezoneOffset,
    Builtins::kDatePrototypeGetUTCDate,
    Builtins::kDatePrototypeGetUTCDay,
    Builtins::kDatePrototypeGetUTCFullYear,
    Builtins::kDatePrototypeGetUTCHours,
    Builtins::kDatePrototypeGetUTCMilliseconds,
    Builtins::kDatePrototypeGetUTCMinutes,
    Builtins::kDatePrototypeGetUTCMonth,
    Builtins::kDatePrototypeGetUTCSeconds,
    Builtins::kDatePrototypeValueOf,
    Builtins::kDatePrototypeToPrimitive,
    Builtins::kDatePrototypeGetYear,
    Builtins::kDatePrototypeSetYear,
    Builtins::kDateNow,
    Builtins::kDateParse,
    Builtins::kDatePrototypeSetDate,
    Builtins::kDatePrototypeSetFullYear,
    Builtins::kDatePrototypeSetHours,
    Builtins::kDatePrototypeSetMilliseconds,
    Builtins::kDatePrototypeSetMinutes,
    Builtins::kDatePrototypeSetMonth,
    Builtins::kDatePrototypeSetSeconds,
    Builtins::kDatePrototypeSetTime,
    Builtins::kDatePrototypeSetUTCDate,
    Builtins::kDatePrototypeSetUTCFullYear,
    Builtins::kDatePrototypeSetUTCHours,
    Builtins::kDatePrototypeSetUTCMilliseconds,
    Builtins::kDatePrototypeSetUTCMinutes,
    Builtins::kDatePrototypeSetUTCMonth,
    Builtins::kDatePrototypeSetUTCSeconds,
    Builtins::kDatePrototypeToDateString,
    Builtins::kDatePrototypeToISOString,
    Builtins::kDatePrototypeToUTCString,
    Builtins::kDatePrototypeToString,
    Builtins::kDatePrototypeToTimeString,
    Builtins::kDatePrototypeToJson,
    Builtins::kDateUTC,
    Builtins::kErrorConstructor,
    Builtins::kErrorCaptureStackTrace,
    Builtins::kErrorPrototypeToString,
    Builtins::kMakeError,
    Builtins::kMakeRangeError,
    Builtins::kMakeSyntaxError,
    Builtins::kMakeTypeError,
    Builtins::kMakeURIError,
    Builtins::kFunctionConstructor,
    Builtins::kFunctionPrototypeApply,
    Builtins::kFunctionPrototypeBind,
    Builtins::kFastFunctionPrototypeBind,
    Builtins::kFunctionPrototypeCall,
    Builtins::kFunctionPrototypeHasInstance,
    Builtins::kFunctionPrototypeToString,
    Builtins::kCreateIterResultObject,
    Builtins::kCreateGeneratorObject,
    Builtins::kGeneratorFunctionConstructor,
    Builtins::kGeneratorPrototypeNext,
    Builtins::kGeneratorPrototypeReturn,
    Builtins::kGeneratorPrototypeThrow,
    Builtins::kAsyncFunctionConstructor,
    Builtins::kGlobalDecodeURI,
    Builtins::kGlobalDecodeURIComponent,
    Builtins::kGlobalEncodeURI,
    Builtins::kGlobalEncodeURIComponent,
    Builtins::kGlobalEscape,
    Builtins::kGlobalUnescape,
    Builtins::kGlobalEval,
    Builtins::kGlobalIsFinite,
    Builtins::kGlobalIsNaN,
    Builtins::kJsonParse,
    Builtins::kJsonStringify,
    Builtins::kLoadIC,
    Builtins::kLoadIC_Megamorphic,
    Builtins::kLoadIC_Noninlined,
    Builtins::kLoadICTrampoline,
    Builtins::kLoadICTrampoline_Megamorphic,
    Builtins::kKeyedLoadIC,
    Builtins::kKeyedLoadIC_Megamorphic,
    Builtins::kKeyedLoadICTrampoline,
    Builtins::kKeyedLoadICTrampoline_Megamorphic,
    Builtins::kStoreGlobalIC,
    Builtins::kStoreGlobalICTrampoline,
    Builtins::kStoreIC,
    Builtins::kStoreICTrampoline,
    Builtins::kKeyedStoreIC,
    Builtins::kKeyedStoreICTrampoline,
    Builtins::kStoreInArrayLiteralIC,
    Builtins::kLoadGlobalIC,
    Builtins::kLoadGlobalICInsideTypeof,
    Builtins::kLoadGlobalICTrampoline,
    Builtins::kLoadGlobalICInsideTypeofTrampoline,
    Builtins::kCloneObjectIC,
    Builtins::kCloneObjectIC_Slow,
    Builtins::kIterableToList,
    Builtins::kIterableToListWithSymbolLookup,
    Builtins::kIterableToListMayPreserveHoles,
    Builtins::kFindOrderedHashMapEntry,
    Builtins::kMapConstructor,
    Builtins::kMapPrototypeSet,
    Builtins::kMapPrototypeDelete,
    Builtins::kMapPrototypeGet,
    Builtins::kMapPrototypeHas,
    Builtins::kMapPrototypeClear,
    Builtins::kMapPrototypeEntries,
    Builtins::kMapPrototypeGetSize,
    Builtins::kMapPrototypeForEach,
    Builtins::kMapPrototypeKeys,
    Builtins::kMapPrototypeValues,
    Builtins::kMapIteratorPrototypeNext,
    Builtins::kMapIteratorToList,
    Builtins::kMathAbs,
    Builtins::kMathAcos,
    Builtins::kMathAcosh,
    Builtins::kMathAsin,
    Builtins::kMathAsinh,
    Builtins::kMathAtan,
    Builtins::kMathAtanh,
    Builtins::kMathAtan2,
    Builtins::kMathCbrt,
    Builtins::kMathCeil,
    Builtins::kMathClz32,
    Builtins::kMathCos,
    Builtins::kMathCosh,
    Builtins::kMathExp,
    Builtins::kMathExpm1,
    Builtins::kMathFloor,
    Builtins::kMathFround,
    Builtins::kMathHypot,
    Builtins::kMathImul,
    Builtins::kMathLog,
    Builtins::kMathLog1p,
    Builtins::kMathLog10,
    Builtins::kMathLog2,
    Builtins::kMathMax,
    Builtins::kMathMin,
    Builtins::kMathPow,
    Builtins::kMathRandom,
    Builtins::kMathRound,
    Builtins::kMathSign,
    Builtins::kMathSin,
    Builtins::kMathSinh,
    Builtins::kMathTan,
    Builtins::kMathTanh,
    Builtins::kMathSqrt,
    Builtins::kMathTrunc,
    Builtins::kAllocateHeapNumber,
    Builtins::kNumberConstructor,
    Builtins::kNumberIsFinite,
    Builtins::kNumberIsInteger,
    Builtins::kNumberIsNaN,
    Builtins::kNumberIsSafeInteger,
    Builtins::kNumberParseFloat,
    Builtins::kNumberParseInt,
    Builtins::kParseInt,
    Builtins::kNumberPrototypeToExponential,
    Builtins::kNumberPrototypeToFixed,
    Builtins::kNumberPrototypeToLocaleString,
    Builtins::kNumberPrototypeToPrecision,
    Builtins::kNumberPrototypeToString,
    Builtins::kNumberPrototypeValueOf,
    Builtins::kAdd,
    Builtins::kSubtract,
    Builtins::kMultiply,
    Builtins::kDivide,
    Builtins::kModulus,
    Builtins::kExponentiate,
    Builtins::kBitwiseAnd,
    Builtins::kBitwiseOr,
    Builtins::kBitwiseXor,
    Builtins::kShiftLeft,
    Builtins::kShiftRight,
    Builtins::kShiftRightLogical,
    Builtins::kLessThan,
    Builtins::kLessThanOrEqual,
    Builtins::kGreaterThan,
    Builtins::kGreaterThanOrEqual,
    Builtins::kEqual,
    Builtins::kSameValue,
    Builtins::kStrictEqual,
    Builtins::kBitwiseNot,
    Builtins::kDecrement,
    Builtins::kIncrement,
    Builtins::kNegate,
    Builtins::kObjectConstructor,
    Builtins::kObjectAssign,
    Builtins::kObjectCreate,
    Builtins::kCreateObjectWithoutProperties,
    Builtins::kObjectDefineGetter,
    Builtins::kObjectDefineProperties,
    Builtins::kObjectDefineProperty,
    Builtins::kObjectDefineSetter,
    Builtins::kObjectEntries,
    Builtins::kObjectFreeze,
    Builtins::kObjectGetOwnPropertyDescriptor,
    Builtins::kObjectGetOwnPropertyDescriptors,
    Builtins::kObjectGetOwnPropertyNames,
    Builtins::kObjectGetOwnPropertySymbols,
    Builtins::kObjectGetPrototypeOf,
    Builtins::kObjectSetPrototypeOf,
    Builtins::kObjectIs,
    Builtins::kObjectIsExtensible,
    Builtins::kObjectIsFrozen,
    Builtins::kObjectIsSealed,
    Builtins::kObjectKeys,
    Builtins::kObjectLookupGetter,
    Builtins::kObjectLookupSetter,
    Builtins::kObjectPreventExtensions,
    Builtins::kObjectPrototypeToString,
    Builtins::kObjectPrototypeValueOf,
    Builtins::kObjectPrototypeHasOwnProperty,
    Builtins::kObjectPrototypeIsPrototypeOf,
    Builtins::kObjectPrototypePropertyIsEnumerable,
    Builtins::kObjectPrototypeGetProto,
    Builtins::kObjectPrototypeSetProto,
    Builtins::kObjectPrototypeToLocaleString,
    Builtins::kObjectSeal,
    Builtins::kObjectToString,
    Builtins::kObjectValues,
    Builtins::kOrdinaryHasInstance,
    Builtins::kInstanceOf,
    Builtins::kForInEnumerate,
    Builtins::kForInFilter,
    Builtins::kFulfillPromise,
    Builtins::kRejectPromise,
    Builtins::kResolvePromise,
    Builtins::kPromiseCapabilityDefaultReject,
    Builtins::kPromiseCapabilityDefaultResolve,
    Builtins::kPromiseGetCapabilitiesExecutor,
    Builtins::kNewPromiseCapability,
    Builtins::kPromiseConstructorLazyDeoptContinuation,
    Builtins::kPromiseConstructor,
    Builtins::kIsPromise,
    Builtins::kPromisePrototypeThen,
    Builtins::kPerformPromiseThen,
    Builtins::kPromisePrototypeCatch,
    Builtins::kPromiseRejectReactionJob,
    Builtins::kPromiseFulfillReactionJob,
    Builtins::kPromiseResolveThenableJob,
    Builtins::kPromiseResolveTrampoline,
    Builtins::kPromiseResolve,
    Builtins::kPromiseReject,
    Builtins::kPromisePrototypeFinally,
    Builtins::kPromiseThenFinally,
    Builtins::kPromiseCatchFinally,
    Builtins::kPromiseValueThunkFinally,
    Builtins::kPromiseThrowerFinally,
    Builtins::kPromiseAll,
    Builtins::kPromiseAllResolveElementClosure,
    Builtins::kPromiseRace,
    Builtins::kPromiseInternalConstructor,
    Builtins::kPromiseInternalReject,
    Builtins::kPromiseInternalResolve,
    Builtins::kProxyConstructor,
    Builtins::kProxyRevocable,
    Builtins::kProxyRevoke,
    Builtins::kProxyGetProperty,
    Builtins::kProxyHasProperty,
    Builtins::kProxySetProperty,
    Builtins::kReflectApply,
    Builtins::kReflectConstruct,
    Builtins::kReflectDefineProperty,
    Builtins::kReflectDeleteProperty,
    Builtins::kReflectGet,
    Builtins::kReflectGetOwnPropertyDescriptor,
    Builtins::kReflectGetPrototypeOf,
    Builtins::kReflectHas,
    Builtins::kReflectIsExtensible,
    Builtins::kReflectOwnKeys,
    Builtins::kReflectPreventExtensions,
    Builtins::kReflectSet,
    Builtins::kReflectSetPrototypeOf,
    Builtins::kRegExpCapture1Getter,
    Builtins::kRegExpCapture2Getter,
    Builtins::kRegExpCapture3Getter,
    Builtins::kRegExpCapture4Getter,
    Builtins::kRegExpCapture5Getter,
    Builtins::kRegExpCapture6Getter,
    Builtins::kRegExpCapture7Getter,
    Builtins::kRegExpCapture8Getter,
    Builtins::kRegExpCapture9Getter,
    Builtins::kRegExpConstructor,
    Builtins::kRegExpInternalMatch,
    Builtins::kRegExpInputGetter,
    Builtins::kRegExpInputSetter,
    Builtins::kRegExpLastMatchGetter,
    Builtins::kRegExpLastParenGetter,
    Builtins::kRegExpLeftContextGetter,
    Builtins::kRegExpPrototypeCompile,
    Builtins::kRegExpPrototypeExec,
    Builtins::kRegExpPrototypeDotAllGetter,
    Builtins::kRegExpPrototypeFlagsGetter,
    Builtins::kRegExpPrototypeGlobalGetter,
    Builtins::kRegExpPrototypeIgnoreCaseGetter,
    Builtins::kRegExpPrototypeMatch,
    Builtins::kRegExpPrototypeMatchAll,
    Builtins::kRegExpPrototypeMultilineGetter,
    Builtins::kRegExpPrototypeSearch,
    Builtins::kRegExpPrototypeSourceGetter,
    Builtins::kRegExpPrototypeStickyGetter,
    Builtins::kRegExpPrototypeTest,
    Builtins::kRegExpPrototypeTestFast,
    Builtins::kRegExpPrototypeToString,
    Builtins::kRegExpPrototypeUnicodeGetter,
    Builtins::kRegExpRightContextGetter,
    Builtins::kRegExpPrototypeReplace,
    Builtins::kRegExpPrototypeSplit,
    Builtins::kRegExpExecAtom,
    Builtins::kRegExpExecInternal,
    Builtins::kRegExpMatchFast,
    Builtins::kRegExpPrototypeExecSlow,
    Builtins::kRegExpReplace,
    Builtins::kRegExpSearchFast,
    Builtins::kRegExpSplit,
    Builtins::kRegExpStringIteratorPrototypeNext,
    Builtins::kSetConstructor,
    Builtins::kSetPrototypeHas,
    Builtins::kSetPrototypeAdd,
    Builtins::kSetPrototypeDelete,
    Builtins::kSetPrototypeClear,
    Builtins::kSetPrototypeEntries,
    Builtins::kSetPrototypeGetSize,
    Builtins::kSetPrototypeForEach,
    Builtins::kSetPrototypeValues,
    Builtins::kSetIteratorPrototypeNext,
    Builtins::kSetOrSetIteratorToList,
    Builtins::kSharedArrayBufferPrototypeGetByteLength,
    Builtins::kSharedArrayBufferPrototypeSlice,
    Builtins::kAtomicsLoad,
    Builtins::kAtomicsStore,
    Builtins::kAtomicsExchange,
    Builtins::kAtomicsCompareExchange,
    Builtins::kAtomicsAdd,
    Builtins::kAtomicsSub,
    Builtins::kAtomicsAnd,
    Builtins::kAtomicsOr,
    Builtins::kAtomicsXor,
    Builtins::kAtomicsNotify,
    Builtins::kAtomicsIsLockFree,
    Builtins::kAtomicsWait,
    Builtins::kAtomicsWake,
    Builtins::kStringConstructor,
    Builtins::kStringFromCodePoint,
    Builtins::kStringFromCharCode,
    Builtins::kStringPrototypeAnchor,
    Builtins::kStringPrototypeBig,
    Builtins::kStringPrototypeBlink,
    Builtins::kStringPrototypeBold,
    Builtins::kStringPrototypeCharAt,
    Builtins::kStringPrototypeCharCodeAt,
    Builtins::kStringPrototypeCodePointAt,
    Builtins::kStringPrototypeConcat,
    Builtins::kStringPrototypeEndsWith,
    Builtins::kStringPrototypeFontcolor,
    Builtins::kStringPrototypeFontsize,
    Builtins::kStringPrototypeFixed,
    Builtins::kStringPrototypeIncludes,
    Builtins::kStringPrototypeIndexOf,
    Builtins::kStringPrototypeItalics,
    Builtins::kStringPrototypeLastIndexOf,
    Builtins::kStringPrototypeLink,
    Builtins::kStringPrototypeMatch,
    Builtins::kStringPrototypeMatchAll,
    Builtins::kStringPrototypeLocaleCompare,
    Builtins::kStringPrototypePadEnd,
    Builtins::kStringPrototypePadStart,
    Builtins::kStringPrototypeRepeat,
    Builtins::kStringPrototypeReplace,
    Builtins::kStringPrototypeSearch,
    Builtins::kStringPrototypeSlice,
    Builtins::kStringPrototypeSmall,
    Builtins::kStringPrototypeSplit,
    Builtins::kStringPrototypeStrike,
    Builtins::kStringPrototypeSub,
    Builtins::kStringPrototypeSubstr,
    Builtins::kStringPrototypeSubstring,
    Builtins::kStringPrototypeSup,
    Builtins::kStringPrototypeStartsWith,
    Builtins::kStringPrototypeToString,
    Builtins::kStringPrototypeTrim,
    Builtins::kStringPrototypeTrimEnd,
    Builtins::kStringPrototypeTrimStart,
    Builtins::kStringPrototypeValueOf,
    Builtins::kStringRaw,
    Builtins::kStringPrototypeIterator,
    Builtins::kStringIteratorPrototypeNext,
    Builtins::kStringToList,
    Builtins::kSymbolConstructor,
    Builtins::kSymbolFor,
    Builtins::kSymbolKeyFor,
    Builtins::kSymbolPrototypeDescriptionGetter,
    Builtins::kSymbolPrototypeToPrimitive,
    Builtins::kSymbolPrototypeToString,
    Builtins::kSymbolPrototypeValueOf,
    Builtins::kTypedArrayInitialize,
    Builtins::kTypedArrayInitializeWithBuffer,
    Builtins::kCreateTypedArray,
    Builtins::kTypedArrayBaseConstructor,
    Builtins::kGenericConstructorLazyDeoptContinuation,
    Builtins::kTypedArrayConstructor,
    Builtins::kTypedArrayPrototypeBuffer,
    Builtins::kTypedArrayPrototypeByteLength,
    Builtins::kTypedArrayPrototypeByteOffset,
    Builtins::kTypedArrayPrototypeLength,
    Builtins::kTypedArrayPrototypeEntries,
    Builtins::kTypedArrayPrototypeKeys,
    Builtins::kTypedArrayPrototypeValues,
    Builtins::kTypedArrayPrototypeCopyWithin,
    Builtins::kTypedArrayPrototypeFill,
    Builtins::kTypedArrayPrototypeFilter,
    Builtins::kTypedArrayPrototypeFind,
    Builtins::kTypedArrayPrototypeFindIndex,
    Builtins::kTypedArrayPrototypeIncludes,
    Builtins::kTypedArrayPrototypeIndexOf,
    Builtins::kTypedArrayPrototypeLastIndexOf,
    Builtins::kTypedArrayPrototypeReverse,
    Builtins::kTypedArrayPrototypeSet,
    Builtins::kTypedArrayPrototypeSlice,
    Builtins::kTypedArrayPrototypeSubArray,
    Builtins::kTypedArrayPrototypeToStringTag,
    Builtins::kTypedArrayPrototypeEvery,
    Builtins::kTypedArrayPrototypeSome,
    Builtins::kTypedArrayPrototypeReduce,
    Builtins::kTypedArrayPrototypeReduceRight,
    Builtins::kTypedArrayPrototypeMap,
    Builtins::kTypedArrayPrototypeForEach,
    Builtins::kTypedArrayOf,
    Builtins::kTypedArrayFrom,
    Builtins::kWasmCompileLazy,
    Builtins::kWasmAllocateHeapNumber,
    Builtins::kWasmCallJavaScript,
    Builtins::kWasmMemoryGrow,
    Builtins::kWasmRecordWrite,
    Builtins::kWasmStackGuard,
    Builtins::kWasmToNumber,
    Builtins::kWasmThrow,
    Builtins::kThrowWasmTrapUnreachable,
    Builtins::kThrowWasmTrapMemOutOfBounds,
    Builtins::kThrowWasmTrapUnalignedAccess,
    Builtins::kThrowWasmTrapDivByZero,
    Builtins::kThrowWasmTrapDivUnrepresentable,
    Builtins::kThrowWasmTrapRemByZero,
    Builtins::kThrowWasmTrapFloatUnrepresentable,
    Builtins::kThrowWasmTrapFuncInvalid,
    Builtins::kThrowWasmTrapFuncSigMismatch,
    Builtins::kWeakMapConstructor,
    Builtins::kWeakMapLookupHashIndex,
    Builtins::kWeakMapGet,
    Builtins::kWeakMapHas,
    Builtins::kWeakMapPrototypeSet,
    Builtins::kWeakMapPrototypeDelete,
    Builtins::kWeakSetConstructor,
    Builtins::kWeakSetHas,
    Builtins::kWeakSetPrototypeAdd,
    Builtins::kWeakSetPrototypeDelete,
    Builtins::kWeakCollectionDelete,
    Builtins::kWeakCollectionSet,
    Builtins::kAsyncGeneratorResolve,
    Builtins::kAsyncGeneratorReject,
    Builtins::kAsyncGeneratorYield,
    Builtins::kAsyncGeneratorReturn,
    Builtins::kAsyncGeneratorResumeNext,
    Builtins::kAsyncGeneratorFunctionConstructor,
    Builtins::kAsyncGeneratorPrototypeNext,
    Builtins::kAsyncGeneratorPrototypeReturn,
    Builtins::kAsyncGeneratorPrototypeThrow,
    Builtins::kAsyncGeneratorAwaitCaught,
    Builtins::kAsyncGeneratorAwaitUncaught,
    Builtins::kAsyncGeneratorAwaitResolveClosure,
    Builtins::kAsyncGeneratorAwaitRejectClosure,
    Builtins::kAsyncGeneratorYieldResolveClosure,
    Builtins::kAsyncGeneratorReturnClosedResolveClosure,
    Builtins::kAsyncGeneratorReturnClosedRejectClosure,
    Builtins::kAsyncGeneratorReturnResolveClosure,
    Builtins::kAsyncFromSyncIteratorPrototypeNext,
    Builtins::kAsyncFromSyncIteratorPrototypeThrow,
    Builtins::kAsyncFromSyncIteratorPrototypeReturn,
    Builtins::kAsyncIteratorValueUnwrap,
    Builtins::kCEntry_Return1_DontSaveFPRegs_ArgvOnStack_NoBuiltinExit,
    Builtins::kCEntry_Return1_DontSaveFPRegs_ArgvOnStack_BuiltinExit,
    Builtins::kCEntry_Return1_DontSaveFPRegs_ArgvInRegister_NoBuiltinExit,
    Builtins::kCEntry_Return1_SaveFPRegs_ArgvOnStack_NoBuiltinExit,
    Builtins::kCEntry_Return1_SaveFPRegs_ArgvOnStack_BuiltinExit,
    Builtins::kCEntry_Return2_DontSaveFPRegs_ArgvOnStack_NoBuiltinExit,
    Builtins::kCEntry_Return2_DontSaveFPRegs_ArgvOnStack_BuiltinExit,
    Builtins::kCEntry_Return2_DontSaveFPRegs_ArgvInRegister_NoBuiltinExit,
    Builtins::kCEntry_Return2_SaveFPRegs_ArgvOnStack_NoBuiltinExit,
    Builtins::kCEntry_Return2_SaveFPRegs_ArgvOnStack_BuiltinExit,
    Builtins::kStringAdd_CheckNone,
    Builtins::kStringAdd_ConvertLeft,
    Builtins::kStringAdd_ConvertRight,
    Builtins::kSubString,
    Builtins::kCallApiCallback_Argc0,
    Builtins::kCallApiCallback_Argc1,
    Builtins::kCallApiGetter,
    Builtins::kDoubleToI,
    Builtins::kGetProperty,
    Builtins::kSetProperty,
    Builtins::kSetPropertyInLiteral,
    Builtins::kMathPowInternal,
    Builtins::kIsTraceCategoryEnabled,
    Builtins::kTrace,
    Builtins::kWeakCellClear,
    Builtins::kWeakCellHoldingsGetter,
    Builtins::kWeakFactoryCleanupIteratorNext,
    Builtins::kWeakFactoryConstructor,
    Builtins::kWeakFactoryMakeCell,
    Builtins::kWeakFactoryMakeRef,
    Builtins::kWeakRefDeref,
    Builtins::kArrayPrototypeCopyWithin,
    Builtins::kArrayForEachLoopEagerDeoptContinuation,
    Builtins::kArrayForEachLoopLazyDeoptContinuation,
    Builtins::kArrayForEachLoopContinuation,
    Builtins::kArrayForEach,
    Builtins::kLoadJoinElement20ATDictionaryElements,
    Builtins::kLoadJoinElement25ATFastSmiOrObjectElements,
    Builtins::kLoadJoinElement20ATFastDoubleElements,
    Builtins::kConvertToLocaleString,
    Builtins::kArrayJoinWithToLocaleString,
    Builtins::kArrayJoinWithoutToLocaleString,
    Builtins::kJoinStackPush,
    Builtins::kJoinStackPop,
    Builtins::kArrayPrototypeJoin,
    Builtins::kArrayPrototypeToLocaleString,
    Builtins::kArrayPrototypeToString,
    Builtins::kArrayPrototypeLastIndexOf,
    Builtins::kArrayOf,
    Builtins::kArrayPrototypeReverse,
    Builtins::kArraySlice,
    Builtins::kArraySplice,
    Builtins::kArrayPrototypeUnshift,
    Builtins::kTypedArrayQuickSort,
    Builtins::kTypedArrayPrototypeSort,
    Builtins::kDataViewPrototypeGetBuffer,
    Builtins::kDataViewPrototypeGetByteLength,
    Builtins::kDataViewPrototypeGetByteOffset,
    Builtins::kDataViewPrototypeGetUint8,
    Builtins::kDataViewPrototypeGetInt8,
    Builtins::kDataViewPrototypeGetUint16,
    Builtins::kDataViewPrototypeGetInt16,
    Builtins::kDataViewPrototypeGetUint32,
    Builtins::kDataViewPrototypeGetInt32,
    Builtins::kDataViewPrototypeGetFloat32,
    Builtins::kDataViewPrototypeGetFloat64,
    Builtins::kDataViewPrototypeGetBigUint64,
    Builtins::kDataViewPrototypeGetBigInt64,
    Builtins::kDataViewPrototypeSetUint8,
    Builtins::kDataViewPrototypeSetInt8,
    Builtins::kDataViewPrototypeSetUint16,
    Builtins::kDataViewPrototypeSetInt16,
    Builtins::kDataViewPrototypeSetUint32,
    Builtins::kDataViewPrototypeSetInt32,
    Builtins::kDataViewPrototypeSetFloat32,
    Builtins::kDataViewPrototypeSetFloat64,
    Builtins::kDataViewPrototypeSetBigUint64,
    Builtins::kDataViewPrototypeSetBigInt64,
    Builtins::kGenericBuiltinTest22UT12ATHeapObject5ATSmi,
    Builtins::kTestHelperPlus1,
    Builtins::kTestHelperPlus2,
    Builtins::kLoad23ATFastPackedSmiElements,
    Builtins::kLoad25ATFastSmiOrObjectElements,
    Builtins::kLoad20ATFastDoubleElements,
    Builtins::kLoad20ATDictionaryElements,
    Builtins::kLoad19ATTempArrayElements,
    Builtins::kStore23ATFastPackedSmiElements,
    Builtins::kStore25ATFastSmiOrObjectElements,
    Builtins::kStore20ATFastDoubleElements,
    Builtins::kStore20ATDictionaryElements,
    Builtins::kStore19ATTempArrayElements,
    Builtins::kSortCompareDefault,
    Builtins::kSortCompareUserFn,
    Builtins::kCanUseSameAccessor25ATGenericElementsAccessor,
    Builtins::kCanUseSameAccessor20ATDictionaryElements,
    Builtins::kCopyFromTempArray,
    Builtins::kCopyWithinSortArray,
    Builtins::kBinaryInsertionSort,
    Builtins::kMergeAt,
    Builtins::kGallopLeft,
    Builtins::kGallopRight,
    Builtins::kArrayTimSort,
    Builtins::kArrayPrototypeSort,
    Builtins::kLoadJoinElement25ATGenericElementsAccessor,
    Builtins::kLoadFixedElement17ATFixedInt32Array,
    Builtins::kStoreFixedElement17ATFixedInt32Array,
    Builtins::kLoadFixedElement19ATFixedFloat32Array,
    Builtins::kStoreFixedElement19ATFixedFloat32Array,
    Builtins::kLoadFixedElement19ATFixedFloat64Array,
    Builtins::kStoreFixedElement19ATFixedFloat64Array,
    Builtins::kLoadFixedElement24ATFixedUint8ClampedArray,
    Builtins::kStoreFixedElement24ATFixedUint8ClampedArray,
    Builtins::kLoadFixedElement21ATFixedBigUint64Array,
    Builtins::kStoreFixedElement21ATFixedBigUint64Array,
    Builtins::kLoadFixedElement20ATFixedBigInt64Array,
    Builtins::kStoreFixedElement20ATFixedBigInt64Array,
    Builtins::kLoadFixedElement17ATFixedUint8Array,
    Builtins::kStoreFixedElement17ATFixedUint8Array,
    Builtins::kLoadFixedElement16ATFixedInt8Array,
    Builtins::kStoreFixedElement16ATFixedInt8Array,
    Builtins::kLoadFixedElement18ATFixedUint16Array,
    Builtins::kStoreFixedElement18ATFixedUint16Array,
    Builtins::kLoadFixedElement17ATFixedInt16Array,
    Builtins::kStoreFixedElement17ATFixedInt16Array,
    Builtins::kLoadFixedElement18ATFixedUint32Array,
    Builtins::kStoreFixedElement18ATFixedUint32Array,
    Builtins::kGenericBuiltinTest5ATSmi,
    Builtins::kLoad25ATGenericElementsAccessor,
    Builtins::kStore25ATGenericElementsAccessor,
    Builtins::kCanUseSameAccessor20ATFastDoubleElements,
    Builtins::kCanUseSameAccessor23ATFastPackedSmiElements,
    Builtins::kCanUseSameAccessor25ATFastSmiOrObjectElements,
    Builtins::kCollatorConstructor,
    Builtins::kCollatorInternalCompare,
    Builtins::kCollatorPrototypeCompare,
    Builtins::kCollatorSupportedLocalesOf,
    Builtins::kCollatorPrototypeResolvedOptions,
    Builtins::kDatePrototypeToLocaleDateString,
    Builtins::kDatePrototypeToLocaleString,
    Builtins::kDatePrototypeToLocaleTimeString,
    Builtins::kDateTimeFormatConstructor,
    Builtins::kDateTimeFormatInternalFormat,
    Builtins::kDateTimeFormatPrototypeFormat,
    Builtins::kDateTimeFormatPrototypeFormatToParts,
    Builtins::kDateTimeFormatPrototypeResolvedOptions,
    Builtins::kDateTimeFormatSupportedLocalesOf,
    Builtins::kIntlGetCanonicalLocales,
    Builtins::kListFormatConstructor,
    Builtins::kListFormatPrototypeFormat,
    Builtins::kListFormatPrototypeFormatToParts,
    Builtins::kListFormatPrototypeResolvedOptions,
    Builtins::kListFormatSupportedLocalesOf,
    Builtins::kLocaleConstructor,
    Builtins::kLocalePrototypeBaseName,
    Builtins::kLocalePrototypeCalendar,
    Builtins::kLocalePrototypeCaseFirst,
    Builtins::kLocalePrototypeCollation,
    Builtins::kLocalePrototypeHourCycle,
    Builtins::kLocalePrototypeLanguage,
    Builtins::kLocalePrototypeMaximize,
    Builtins::kLocalePrototypeMinimize,
    Builtins::kLocalePrototypeNumeric,
    Builtins::kLocalePrototypeNumberingSystem,
    Builtins::kLocalePrototypeRegion,
    Builtins::kLocalePrototypeScript,
    Builtins::kLocalePrototypeToString,
    Builtins::kNumberFormatConstructor,
    Builtins::kNumberFormatInternalFormatNumber,
    Builtins::kNumberFormatPrototypeFormatNumber,
    Builtins::kNumberFormatPrototypeFormatToParts,
    Builtins::kNumberFormatPrototypeResolvedOptions,
    Builtins::kNumberFormatSupportedLocalesOf,
    Builtins::kPluralRulesConstructor,
    Builtins::kPluralRulesPrototypeResolvedOptions,
    Builtins::kPluralRulesPrototypeSelect,
    Builtins::kPluralRulesSupportedLocalesOf,
    Builtins::kRelativeTimeFormatConstructor,
    Builtins::kRelativeTimeFormatPrototypeFormat,
    Builtins::kRelativeTimeFormatPrototypeFormatToParts,
    Builtins::kRelativeTimeFormatPrototypeResolvedOptions,
    Builtins::kRelativeTimeFormatSupportedLocalesOf,
    Builtins::kSegmenterConstructor,
    Builtins::kSegmenterPrototypeResolvedOptions,
    Builtins::kSegmenterPrototypeSegment,
    Builtins::kSegmenterSupportedLocalesOf,
    Builtins::kSegmentIteratorPrototypeBreakType,
    Builtins::kSegmentIteratorPrototypeFollowing,
    Builtins::kSegmentIteratorPrototypePreceding,
    Builtins::kSegmentIteratorPrototypePosition,
    Builtins::kSegmentIteratorPrototypeNext,
    Builtins::kStringPrototypeNormalizeIntl,
    Builtins::kStringPrototypeToLocaleLowerCase,
    Builtins::kStringPrototypeToLocaleUpperCase,
    Builtins::kStringPrototypeToLowerCaseIntl,
    Builtins::kStringPrototypeToUpperCaseIntl,
    Builtins::kStringToLowerCaseIntl,
    Builtins::kV8BreakIteratorConstructor,
    Builtins::kV8BreakIteratorInternalAdoptText,
    Builtins::kV8BreakIteratorInternalBreakType,
    Builtins::kV8BreakIteratorInternalCurrent,
    Builtins::kV8BreakIteratorInternalFirst,
    Builtins::kV8BreakIteratorInternalNext,
    Builtins::kV8BreakIteratorPrototypeAdoptText,
    Builtins::kV8BreakIteratorPrototypeBreakType,
    Builtins::kV8BreakIteratorPrototypeCurrent,
    Builtins::kV8BreakIteratorPrototypeFirst,
    Builtins::kV8BreakIteratorPrototypeNext,
    Builtins::kV8BreakIteratorPrototypeResolvedOptions,
    Builtins::kV8BreakIteratorSupportedLocalesOf,
    Builtins::kWideHandler,
    Builtins::kExtraWideHandler,
    Builtins::kDebugBreakWideHandler,
    Builtins::kDebugBreakExtraWideHandler,
    Builtins::kDebugBreak0Handler,
    Builtins::kDebugBreak1Handler,
    Builtins::kDebugBreak2Handler,
    Builtins::kDebugBreak3Handler,
    Builtins::kDebugBreak4Handler,
    Builtins::kDebugBreak5Handler,
    Builtins::kDebugBreak6Handler,
    Builtins::kLdaLookupContextSlotHandler,
    Builtins::kLdaLookupGlobalSlotHandler,
    Builtins::kLdaLookupSlotInsideTypeofHandler,
    Builtins::kLdaLookupContextSlotInsideTypeofHandler,
    Builtins::kLdaLookupGlobalSlotInsideTypeofHandler,
    Builtins::kLdaModuleVariableHandler,
    Builtins::kStaModuleVariableHandler,
    Builtins::kStaDataPropertyInLiteralHandler,
    Builtins::kCollectTypeProfileHandler,
    Builtins::kModHandler,
    Builtins::kExpHandler,
    Builtins::kShiftRightHandler,
    Builtins::kShiftRightLogicalHandler,
    Builtins::kExpSmiHandler,
    Builtins::kShiftLeftSmiHandler,
    Builtins::kShiftRightSmiHandler,
    Builtins::kShiftRightLogicalSmiHandler,
    Builtins::kGetSuperConstructorHandler,
    Builtins::kCallWithSpreadHandler,
    Builtins::kCallJSRuntimeHandler,
    Builtins::kConstructWithSpreadHandler,
    Builtins::kToNameHandler,
    Builtins::kCreateArrayFromIterableHandler,
    Builtins::kCloneObjectHandler,
    Builtins::kGetTemplateObjectHandler,
    Builtins::kCreateEvalContextHandler,
    Builtins::kCreateRestParameterHandler,
    Builtins::kJumpIfNotNullConstantHandler,
    Builtins::kJumpIfNotUndefinedConstantHandler,
    Builtins::kJumpIfJSReceiverConstantHandler,
    Builtins::kThrowSuperNotCalledIfHoleHandler,
    Builtins::kThrowSuperAlreadyCalledIfNotHoleHandler,
    Builtins::kSwitchOnGeneratorStateHandler,
    Builtins::kSuspendGeneratorHandler,
    Builtins::kResumeGeneratorHandler,
    Builtins::kDebuggerHandler,
    Builtins::kIncBlockCounterHandler,
    Builtins::kAbortHandler,
    Builtins::kIllegalHandler,
    Builtins::kDebugBreak1WideHandler,
    Builtins::kDebugBreak2WideHandler,
    Builtins::kDebugBreak3WideHandler,
    Builtins::kDebugBreak4WideHandler,
    Builtins::kDebugBreak5WideHandler,
    Builtins::kDebugBreak6WideHandler,
    Builtins::kLdaSmiWideHandler,
    Builtins::kLdaConstantWideHandler,
    Builtins::kLdaGlobalWideHandler,
    Builtins::kLdaGlobalInsideTypeofWideHandler,
    Builtins::kStaGlobalWideHandler,
    Builtins::kPushContextWideHandler,
    Builtins::kPopContextWideHandler,
    Builtins::kLdaContextSlotWideHandler,
    Builtins::kLdaImmutableContextSlotWideHandler,
    Builtins::kLdaCurrentContextSlotWideHandler,
    Builtins::kLdaImmutableCurrentContextSlotWideHandler,
    Builtins::kStaContextSlotWideHandler,
    Builtins::kStaCurrentContextSlotWideHandler,
    Builtins::kLdaLookupSlotWideHandler,
    Builtins::kLdaLookupContextSlotWideHandler,
    Builtins::kLdaLookupGlobalSlotWideHandler,
    Builtins::kLdaLookupSlotInsideTypeofWideHandler,
    Builtins::kLdaLookupContextSlotInsideTypeofWideHandler,
    Builtins::kLdaLookupGlobalSlotInsideTypeofWideHandler,
    Builtins::kStaLookupSlotWideHandler,
    Builtins::kLdarWideHandler,
    Builtins::kStarWideHandler,
    Builtins::kMovWideHandler,
    Builtins::kLdaNamedPropertyWideHandler,
    Builtins::kLdaNamedPropertyNoFeedbackWideHandler,
    Builtins::kLdaKeyedPropertyWideHandler,
    Builtins::kLdaModuleVariableWideHandler,
    Builtins::kStaModuleVariableWideHandler,
    Builtins::kStaNamedPropertyWideHandler,
    Builtins::kStaNamedPropertyNoFeedbackWideHandler,
    Builtins::kStaNamedOwnPropertyWideHandler,
    Builtins::kStaKeyedPropertyWideHandler,
    Builtins::kStaInArrayLiteralWideHandler,
    Builtins::kStaDataPropertyInLiteralWideHandler,
    Builtins::kCollectTypeProfileWideHandler,
    Builtins::kAddWideHandler,
    Builtins::kSubWideHandler,
    Builtins::kMulWideHandler,
    Builtins::kDivWideHandler,
    Builtins::kModWideHandler,
    Builtins::kExpWideHandler,
    Builtins::kBitwiseOrWideHandler,
    Builtins::kBitwiseXorWideHandler,
    Builtins::kBitwiseAndWideHandler,
    Builtins::kShiftLeftWideHandler,
    Builtins::kShiftRightWideHandler,
    Builtins::kShiftRightLogicalWideHandler,
    Builtins::kAddSmiWideHandler,
    Builtins::kSubSmiWideHandler,
    Builtins::kMulSmiWideHandler,
    Builtins::kDivSmiWideHandler,
    Builtins::kModSmiWideHandler,
    Builtins::kExpSmiWideHandler,
    Builtins::kBitwiseOrSmiWideHandler,
    Builtins::kBitwiseXorSmiWideHandler,
    Builtins::kBitwiseAndSmiWideHandler,
    Builtins::kShiftLeftSmiWideHandler,
    Builtins::kShiftRightSmiWideHandler,
    Builtins::kShiftRightLogicalSmiWideHandler,
    Builtins::kIncWideHandler,
    Builtins::kDecWideHandler,
    Builtins::kNegateWideHandler,
    Builtins::kBitwiseNotWideHandler,
    Builtins::kDeletePropertyStrictWideHandler,
    Builtins::kDeletePropertySloppyWideHandler,
    Builtins::kGetSuperConstructorWideHandler,
    Builtins::kCallAnyReceiverWideHandler,
    Builtins::kCallPropertyWideHandler,
    Builtins::kCallProperty0WideHandler,
    Builtins::kCallProperty1WideHandler,
    Builtins::kCallProperty2WideHandler,
    Builtins::kCallUndefinedReceiverWideHandler,
    Builtins::kCallUndefinedReceiver0WideHandler,
    Builtins::kCallUndefinedReceiver1WideHandler,
    Builtins::kCallUndefinedReceiver2WideHandler,
    Builtins::kCallNoFeedbackWideHandler,
    Builtins::kCallWithSpreadWideHandler,
    Builtins::kCallRuntimeWideHandler,
    Builtins::kCallRuntimeForPairWideHandler,
    Builtins::kCallJSRuntimeWideHandler,
    Builtins::kInvokeIntrinsicWideHandler,
    Builtins::kConstructWideHandler,
    Builtins::kConstructWithSpreadWideHandler,
    Builtins::kTestEqualWideHandler,
    Builtins::kTestEqualStrictWideHandler,
    Builtins::kTestLessThanWideHandler,
    Builtins::kTestGreaterThanWideHandler,
    Builtins::kTestLessThanOrEqualWideHandler,
    Builtins::kTestGreaterThanOrEqualWideHandler,
    Builtins::kTestReferenceEqualWideHandler,
    Builtins::kTestInstanceOfWideHandler,
    Builtins::kTestInWideHandler,
    Builtins::kToNameWideHandler,
    Builtins::kToNumberWideHandler,
    Builtins::kToNumericWideHandler,
    Builtins::kToObjectWideHandler,
    Builtins::kCreateRegExpLiteralWideHandler,
    Builtins::kCreateArrayLiteralWideHandler,
    Builtins::kCreateEmptyArrayLiteralWideHandler,
    Builtins::kCreateObjectLiteralWideHandler,
    Builtins::kCloneObjectWideHandler,
    Builtins::kGetTemplateObjectWideHandler,
    Builtins::kCreateClosureWideHandler,
    Builtins::kCreateBlockContextWideHandler,
    Builtins::kCreateCatchContextWideHandler,
    Builtins::kCreateFunctionContextWideHandler,
    Builtins::kCreateEvalContextWideHandler,
    Builtins::kCreateWithContextWideHandler,
    Builtins::kJumpLoopWideHandler,
    Builtins::kJumpWideHandler,
    Builtins::kJumpConstantWideHandler,
    Builtins::kJumpIfNullConstantWideHandler,
    Builtins::kJumpIfNotNullConstantWideHandler,
    Builtins::kJumpIfUndefinedConstantWideHandler,
    Builtins::kJumpIfNotUndefinedConstantWideHandler,
    Builtins::kJumpIfTrueConstantWideHandler,
    Builtins::kJumpIfFalseConstantWideHandler,
    Builtins::kJumpIfJSReceiverConstantWideHandler,
    Builtins::kJumpIfToBooleanTrueConstantWideHandler,
    Builtins::kJumpIfToBooleanFalseConstantWideHandler,
    Builtins::kJumpIfToBooleanTrueWideHandler,
    Builtins::kJumpIfToBooleanFalseWideHandler,
    Builtins::kJumpIfTrueWideHandler,
    Builtins::kJumpIfFalseWideHandler,
    Builtins::kJumpIfNullWideHandler,
    Builtins::kJumpIfNotNullWideHandler,
    Builtins::kJumpIfUndefinedWideHandler,
    Builtins::kJumpIfNotUndefinedWideHandler,
    Builtins::kJumpIfJSReceiverWideHandler,
    Builtins::kSwitchOnSmiNoFeedbackWideHandler,
    Builtins::kForInEnumerateWideHandler,
    Builtins::kForInPrepareWideHandler,
    Builtins::kForInContinueWideHandler,
    Builtins::kForInNextWideHandler,
    Builtins::kForInStepWideHandler,
    Builtins::kThrowReferenceErrorIfHoleWideHandler,
    Builtins::kSwitchOnGeneratorStateWideHandler,
    Builtins::kSuspendGeneratorWideHandler,
    Builtins::kResumeGeneratorWideHandler,
    Builtins::kIncBlockCounterWideHandler,
    Builtins::kAbortWideHandler,
    Builtins::kDebugBreak1ExtraWideHandler,
    Builtins::kDebugBreak2ExtraWideHandler,
    Builtins::kDebugBreak3ExtraWideHandler,
    Builtins::kDebugBreak4ExtraWideHandler,
    Builtins::kDebugBreak5ExtraWideHandler,
    Builtins::kDebugBreak6ExtraWideHandler,
    Builtins::kLdaSmiExtraWideHandler,
    Builtins::kLdaConstantExtraWideHandler,
    Builtins::kLdaGlobalExtraWideHandler,
    Builtins::kLdaGlobalInsideTypeofExtraWideHandler,
    Builtins::kStaGlobalExtraWideHandler,
    Builtins::kPushContextExtraWideHandler,
    Builtins::kPopContextExtraWideHandler,
    Builtins::kLdaContextSlotExtraWideHandler,
    Builtins::kLdaImmutableContextSlotExtraWideHandler,
    Builtins::kLdaCurrentContextSlotExtraWideHandler,
    Builtins::kLdaImmutableCurrentContextSlotExtraWideHandler,
    Builtins::kStaContextSlotExtraWideHandler,
    Builtins::kStaCurrentContextSlotExtraWideHandler,
    Builtins::kLdaLookupSlotExtraWideHandler,
    Builtins::kLdaLookupContextSlotExtraWideHandler,
    Builtins::kLdaLookupGlobalSlotExtraWideHandler,
    Builtins::kLdaLookupSlotInsideTypeofExtraWideHandler,
    Builtins::kLdaLookupContextSlotInsideTypeofExtraWideHandler,
    Builtins::kLdaLookupGlobalSlotInsideTypeofExtraWideHandler,
    Builtins::kStaLookupSlotExtraWideHandler,
    Builtins::kLdarExtraWideHandler,
    Builtins::kStarExtraWideHandler,
    Builtins::kMovExtraWideHandler,
    Builtins::kLdaNamedPropertyExtraWideHandler,
    Builtins::kLdaNamedPropertyNoFeedbackExtraWideHandler,
    Builtins::kLdaKeyedPropertyExtraWideHandler,
    Builtins::kLdaModuleVariableExtraWideHandler,
    Builtins::kStaModuleVariableExtraWideHandler,
    Builtins::kStaNamedPropertyExtraWideHandler,
    Builtins::kStaNamedPropertyNoFeedbackExtraWideHandler,
    Builtins::kStaNamedOwnPropertyExtraWideHandler,
    Builtins::kStaKeyedPropertyExtraWideHandler,
    Builtins::kStaInArrayLiteralExtraWideHandler,
    Builtins::kStaDataPropertyInLiteralExtraWideHandler,
    Builtins::kCollectTypeProfileExtraWideHandler,
    Builtins::kAddExtraWideHandler,
    Builtins::kSubExtraWideHandler,
    Builtins::kMulExtraWideHandler,
    Builtins::kDivExtraWideHandler,
    Builtins::kModExtraWideHandler,
    Builtins::kExpExtraWideHandler,
    Builtins::kBitwiseOrExtraWideHandler,
    Builtins::kBitwiseXorExtraWideHandler,
    Builtins::kBitwiseAndExtraWideHandler,
    Builtins::kShiftLeftExtraWideHandler,
    Builtins::kShiftRightExtraWideHandler,
    Builtins::kShiftRightLogicalExtraWideHandler,
    Builtins::kAddSmiExtraWideHandler,
    Builtins::kSubSmiExtraWideHandler,
    Builtins::kMulSmiExtraWideHandler,
    Builtins::kDivSmiExtraWideHandler,
    Builtins::kModSmiExtraWideHandler,
    Builtins::kExpSmiExtraWideHandler,
    Builtins::kBitwiseOrSmiExtraWideHandler,
    Builtins::kBitwiseXorSmiExtraWideHandler,
    Builtins::kBitwiseAndSmiExtraWideHandler,
    Builtins::kShiftLeftSmiExtraWideHandler,
    Builtins::kShiftRightSmiExtraWideHandler,
    Builtins::kShiftRightLogicalSmiExtraWideHandler,
    Builtins::kIncExtraWideHandler,
    Builtins::kDecExtraWideHandler,
    Builtins::kNegateExtraWideHandler,
    Builtins::kBitwiseNotExtraWideHandler,
    Builtins::kDeletePropertyStrictExtraWideHandler,
    Builtins::kDeletePropertySloppyExtraWideHandler,
    Builtins::kGetSuperConstructorExtraWideHandler,
    Builtins::kCallAnyReceiverExtraWideHandler,
    Builtins::kCallPropertyExtraWideHandler,
    Builtins::kCallProperty0ExtraWideHandler,
    Builtins::kCallProperty1ExtraWideHandler,
    Builtins::kCallProperty2ExtraWideHandler,
    Builtins::kCallUndefinedReceiverExtraWideHandler,
    Builtins::kCallUndefinedReceiver0ExtraWideHandler,
    Builtins::kCallUndefinedReceiver1ExtraWideHandler,
    Builtins::kCallUndefinedReceiver2ExtraWideHandler,
    Builtins::kCallNoFeedbackExtraWideHandler,
    Builtins::kCallWithSpreadExtraWideHandler,
    Builtins::kCallRuntimeExtraWideHandler,
    Builtins::kCallRuntimeForPairExtraWideHandler,
    Builtins::kCallJSRuntimeExtraWideHandler,
    Builtins::kInvokeIntrinsicExtraWideHandler,
    Builtins::kConstructExtraWideHandler,
    Builtins::kConstructWithSpreadExtraWideHandler,
    Builtins::kTestEqualExtraWideHandler,
    Builtins::kTestEqualStrictExtraWideHandler,
    Builtins::kTestLessThanExtraWideHandler,
    Builtins::kTestGreaterThanExtraWideHandler,
    Builtins::kTestLessThanOrEqualExtraWideHandler,
    Builtins::kTestGreaterThanOrEqualExtraWideHandler,
    Builtins::kTestReferenceEqualExtraWideHandler,
    Builtins::kTestInstanceOfExtraWideHandler,
    Builtins::kTestInExtraWideHandler,
    Builtins::kToNameExtraWideHandler,
    Builtins::kToNumberExtraWideHandler,
    Builtins::kToNumericExtraWideHandler,
    Builtins::kToObjectExtraWideHandler,
    Builtins::kCreateRegExpLiteralExtraWideHandler,
    Builtins::kCreateArrayLiteralExtraWideHandler,
    Builtins::kCreateEmptyArrayLiteralExtraWideHandler,
    Builtins::kCreateObjectLiteralExtraWideHandler,
    Builtins::kCloneObjectExtraWideHandler,
    Builtins::kGetTemplateObjectExtraWideHandler,
    Builtins::kCreateClosureExtraWideHandler,
    Builtins::kCreateBlockContextExtraWideHandler,
    Builtins::kCreateCatchContextExtraWideHandler,
    Builtins::kCreateFunctionContextExtraWideHandler,
    Builtins::kCreateEvalContextExtraWideHandler,
    Builtins::kCreateWithContextExtraWideHandler,
    Builtins::kJumpLoopExtraWideHandler,
    Builtins::kJumpExtraWideHandler,
    Builtins::kJumpConstantExtraWideHandler,
    Builtins::kJumpIfNullConstantExtraWideHandler,
    Builtins::kJumpIfNotNullConstantExtraWideHandler,
    Builtins::kJumpIfUndefinedConstantExtraWideHandler,
    Builtins::kJumpIfNotUndefinedConstantExtraWideHandler,
    Builtins::kJumpIfTrueConstantExtraWideHandler,
    Builtins::kJumpIfFalseConstantExtraWideHandler,
    Builtins::kJumpIfJSReceiverConstantExtraWideHandler,
    Builtins::kJumpIfToBooleanTrueConstantExtraWideHandler,
    Builtins::kJumpIfToBooleanFalseConstantExtraWideHandler,
    Builtins::kJumpIfToBooleanTrueExtraWideHandler,
    Builtins::kJumpIfToBooleanFalseExtraWideHandler,
    Builtins::kJumpIfTrueExtraWideHandler,
    Builtins::kJumpIfFalseExtraWideHandler,
    Builtins::kJumpIfNullExtraWideHandler,
    Builtins::kJumpIfNotNullExtraWideHandler,
    Builtins::kJumpIfUndefinedExtraWideHandler,
    Builtins::kJumpIfNotUndefinedExtraWideHandler,
    Builtins::kJumpIfJSReceiverExtraWideHandler,
    Builtins::kSwitchOnSmiNoFeedbackExtraWideHandler,
    Builtins::kForInEnumerateExtraWideHandler,
    Builtins::kForInPrepareExtraWideHandler,
    Builtins::kForInContinueExtraWideHandler,
    Builtins::kForInNextExtraWideHandler,
    Builtins::kForInStepExtraWideHandler,
    Builtins::kThrowReferenceErrorIfHoleExtraWideHandler,
    Builtins::kSwitchOnGeneratorStateExtraWideHandler,
    Builtins::kSuspendGeneratorExtraWideHandler,
    Builtins::kResumeGeneratorExtraWideHandler,
    Builtins::kIncBlockCounterExtraWideHandler,
    Builtins::kAbortExtraWideHandler,
};

constexpr int MapEmbeddedIndexToBuiltinIndex(int embedded_index) {
  STATIC_ASSERT(arraysize(kIndexMap) == EmbeddedData::kBuiltinCount);
  return kIndexMap[embedded_index];
}

constexpr int MapBuiltinIndexToEmbeddedIndex(int builtin_index) {
  switch (builtin_index) {
  case Builtins::kStackCheckHandler: return 0;
  case Builtins::kCreateClosureHandler: return 1;
  case Builtins::kStarHandler: return 2;
  case Builtins::kReturnHandler: return 3;
  case Builtins::kLdaUndefinedHandler: return 4;
  case Builtins::kCallNoFeedbackHandler: return 5;
  case Builtins::kCreateFunctionContextHandler: return 6;
  case Builtins::kPushContextHandler: return 7;
  case Builtins::kStaCurrentContextSlotHandler: return 8;
  case Builtins::kLdaGlobalHandler: return 9;
  case Builtins::kLdaConstantHandler: return 10;
  case Builtins::kLdaSmiHandler: return 11;
  case Builtins::kCallRuntimeHandler: return 12;
  case Builtins::kLdaZeroHandler: return 13;
  case Builtins::kStaInArrayLiteralHandler: return 14;
  case Builtins::kLdarHandler: return 15;
  case Builtins::kLdaImmutableCurrentContextSlotHandler: return 16;
  case Builtins::kLdaNamedPropertyNoFeedbackHandler: return 17;
  case Builtins::kStaNamedOwnPropertyHandler: return 18;
  case Builtins::kStaNamedPropertyHandler: return 19;
  case Builtins::kLdaNamedPropertyHandler: return 20;
  case Builtins::kLdaFalseHandler: return 21;
  case Builtins::kMovHandler: return 22;
  case Builtins::kCallUndefinedReceiverHandler: return 23;
  case Builtins::kJumpIfToBooleanFalseHandler: return 24;
  case Builtins::kLdaTrueHandler: return 25;
  case Builtins::kCallUndefinedReceiver2Handler: return 26;
  case Builtins::kCallProperty1Handler: return 27;
  case Builtins::kLdaImmutableContextSlotHandler: return 28;
  case Builtins::kLdaKeyedPropertyHandler: return 29;
  case Builtins::kStaKeyedPropertyHandler: return 30;
  case Builtins::kJumpIfToBooleanTrueHandler: return 31;
  case Builtins::kCreateEmptyObjectLiteralHandler: return 32;
  case Builtins::kStaGlobalHandler: return 33;
  case Builtins::kStaNamedPropertyNoFeedbackHandler: return 34;
  case Builtins::kCallUndefinedReceiver0Handler: return 35;
  case Builtins::kCallUndefinedReceiver1Handler: return 36;
  case Builtins::kAddHandler: return 37;
  case Builtins::kCreateArrayLiteralHandler: return 38;
  case Builtins::kCallPropertyHandler: return 39;
  case Builtins::kLdaTheHoleHandler: return 40;
  case Builtins::kCreateRegExpLiteralHandler: return 41;
  case Builtins::kTestEqualHandler: return 42;
  case Builtins::kJumpIfFalseHandler: return 43;
  case Builtins::kCallProperty0Handler: return 44;
  case Builtins::kJumpIfJSReceiverHandler: return 45;
  case Builtins::kInvokeIntrinsicHandler: return 46;
  case Builtins::kToBooleanLogicalNotHandler: return 47;
  case Builtins::kJumpLoopHandler: return 48;
  case Builtins::kJumpHandler: return 49;
  case Builtins::kCreateCatchContextHandler: return 50;
  case Builtins::kTestEqualStrictHandler: return 51;
  case Builtins::kPopContextHandler: return 52;
  case Builtins::kSetPendingMessageHandler: return 53;
  case Builtins::kJumpIfTrueHandler: return 54;
  case Builtins::kTestUndetectableHandler: return 55;
  case Builtins::kTestTypeOfHandler: return 56;
  case Builtins::kThrowHandler: return 57;
  case Builtins::kTestReferenceEqualHandler: return 58;
  case Builtins::kReThrowHandler: return 59;
  case Builtins::kCallProperty2Handler: return 60;
  case Builtins::kCreateUnmappedArgumentsHandler: return 61;
  case Builtins::kConstructHandler: return 62;
  case Builtins::kLdaGlobalInsideTypeofHandler: return 63;
  case Builtins::kJumpIfUndefinedHandler: return 64;
  case Builtins::kJumpIfNullHandler: return 65;
  case Builtins::kToObjectHandler: return 66;
  case Builtins::kForInEnumerateHandler: return 67;
  case Builtins::kForInPrepareHandler: return 68;
  case Builtins::kForInContinueHandler: return 69;
  case Builtins::kForInNextHandler: return 70;
  case Builtins::kForInStepHandler: return 71;
  case Builtins::kCreateObjectLiteralHandler: return 72;
  case Builtins::kTestLessThanHandler: return 73;
  case Builtins::kLdaNullHandler: return 74;
  case Builtins::kLdaCurrentContextSlotHandler: return 75;
  case Builtins::kThrowReferenceErrorIfHoleHandler: return 76;
  case Builtins::kCallRuntimeForPairHandler: return 77;
  case Builtins::kCallAnyReceiverHandler: return 78;
  case Builtins::kTestGreaterThanHandler: return 79;
  case Builtins::kJumpIfFalseConstantHandler: return 80;
  case Builtins::kIncHandler: return 81;
  case Builtins::kMulHandler: return 82;
  case Builtins::kCreateEmptyArrayLiteralHandler: return 83;
  case Builtins::kJumpConstantHandler: return 84;
  case Builtins::kLogicalNotHandler: return 85;
  case Builtins::kTypeOfHandler: return 86;
  case Builtins::kTestInstanceOfHandler: return 87;
  case Builtins::kSubHandler: return 88;
  case Builtins::kToNumericHandler: return 89;
  case Builtins::kMulSmiHandler: return 90;
  case Builtins::kDivHandler: return 91;
  case Builtins::kTestGreaterThanOrEqualHandler: return 92;
  case Builtins::kToStringHandler: return 93;
  case Builtins::kLdaContextSlotHandler: return 94;
  case Builtins::kTestInHandler: return 95;
  case Builtins::kTestUndefinedHandler: return 96;
  case Builtins::kDeletePropertyStrictHandler: return 97;
  case Builtins::kTestLessThanOrEqualHandler: return 98;
  case Builtins::kBitwiseOrHandler: return 99;
  case Builtins::kStaContextSlotHandler: return 100;
  case Builtins::kJumpIfToBooleanTrueConstantHandler: return 101;
  case Builtins::kJumpIfUndefinedConstantHandler: return 102;
  case Builtins::kJumpIfNullConstantHandler: return 103;
  case Builtins::kBitwiseAndHandler: return 104;
  case Builtins::kJumpIfToBooleanFalseConstantHandler: return 105;
  case Builtins::kJumpIfTrueConstantHandler: return 106;
  case Builtins::kTestNullHandler: return 107;
  case Builtins::kJumpIfNotUndefinedHandler: return 108;
  case Builtins::kSubSmiHandler: return 109;
  case Builtins::kAddSmiHandler: return 110;
  case Builtins::kDecHandler: return 111;
  case Builtins::kBitwiseNotHandler: return 112;
  case Builtins::kNegateHandler: return 113;
  case Builtins::kJumpIfNotNullHandler: return 114;
  case Builtins::kBitwiseOrSmiHandler: return 115;
  case Builtins::kBitwiseAndSmiHandler: return 116;
  case Builtins::kSwitchOnSmiNoFeedbackHandler: return 117;
  case Builtins::kToNumberHandler: return 118;
  case Builtins::kDeletePropertySloppyHandler: return 119;
  case Builtins::kShiftLeftHandler: return 120;
  case Builtins::kBitwiseXorHandler: return 121;
  case Builtins::kCreateBlockContextHandler: return 122;
  case Builtins::kCreateMappedArgumentsHandler: return 123;
  case Builtins::kBitwiseXorSmiHandler: return 124;
  case Builtins::kCreateWithContextHandler: return 125;
  case Builtins::kLdaLookupSlotHandler: return 126;
  case Builtins::kStaLookupSlotHandler: return 127;
  case Builtins::kDivSmiHandler: return 128;
  case Builtins::kModSmiHandler: return 129;
  case Builtins::kRecordWrite: return 130;
  case Builtins::kAdaptorWithExitFrame: return 131;
  case Builtins::kAdaptorWithBuiltinExitFrame: return 132;
  case Builtins::kArgumentsAdaptorTrampoline: return 133;
  case Builtins::kCallFunction_ReceiverIsNullOrUndefined: return 134;
  case Builtins::kCallFunction_ReceiverIsNotNullOrUndefined: return 135;
  case Builtins::kCallFunction_ReceiverIsAny: return 136;
  case Builtins::kCallBoundFunction: return 137;
  case Builtins::kCall_ReceiverIsNullOrUndefined: return 138;
  case Builtins::kCall_ReceiverIsNotNullOrUndefined: return 139;
  case Builtins::kCall_ReceiverIsAny: return 140;
  case Builtins::kCallProxy: return 141;
  case Builtins::kCallVarargs: return 142;
  case Builtins::kCallWithSpread: return 143;
  case Builtins::kCallWithArrayLike: return 144;
  case Builtins::kCallForwardVarargs: return 145;
  case Builtins::kCallFunctionForwardVarargs: return 146;
  case Builtins::kConstructFunction: return 147;
  case Builtins::kConstructBoundFunction: return 148;
  case Builtins::kConstructedNonConstructable: return 149;
  case Builtins::kConstruct: return 150;
  case Builtins::kConstructVarargs: return 151;
  case Builtins::kConstructWithSpread: return 152;
  case Builtins::kConstructWithArrayLike: return 153;
  case Builtins::kConstructForwardVarargs: return 154;
  case Builtins::kConstructFunctionForwardVarargs: return 155;
  case Builtins::kJSConstructStubGeneric: return 156;
  case Builtins::kJSBuiltinsConstructStub: return 157;
  case Builtins::kFastNewObject: return 158;
  case Builtins::kFastNewClosure: return 159;
  case Builtins::kFastNewFunctionContextEval: return 160;
  case Builtins::kFastNewFunctionContextFunction: return 161;
  case Builtins::kCreateRegExpLiteral: return 162;
  case Builtins::kCreateEmptyArrayLiteral: return 163;
  case Builtins::kCreateShallowArrayLiteral: return 164;
  case Builtins::kCreateShallowObjectLiteral: return 165;
  case Builtins::kConstructProxy: return 166;
  case Builtins::kJSEntryTrampoline: return 167;
  case Builtins::kJSConstructEntryTrampoline: return 168;
  case Builtins::kResumeGeneratorTrampoline: return 169;
  case Builtins::kInterruptCheck: return 170;
  case Builtins::kStackCheck: return 171;
  case Builtins::kStringCharAt: return 172;
  case Builtins::kStringCodePointAtUTF16: return 173;
  case Builtins::kStringCodePointAtUTF32: return 174;
  case Builtins::kStringEqual: return 175;
  case Builtins::kStringGreaterThan: return 176;
  case Builtins::kStringGreaterThanOrEqual: return 177;
  case Builtins::kStringIndexOf: return 178;
  case Builtins::kStringLessThan: return 179;
  case Builtins::kStringLessThanOrEqual: return 180;
  case Builtins::kStringRepeat: return 181;
  case Builtins::kStringSubstring: return 182;
  case Builtins::kOrderedHashTableHealIndex: return 183;
  case Builtins::kInterpreterEntryTrampoline: return 184;
  case Builtins::kInterpreterPushArgsThenCall: return 185;
  case Builtins::kInterpreterPushUndefinedAndArgsThenCall: return 186;
  case Builtins::kInterpreterPushArgsThenCallWithFinalSpread: return 187;
  case Builtins::kInterpreterPushArgsThenConstruct: return 188;
  case Builtins::kInterpreterPushArgsThenConstructArrayFunction: return 189;
  case Builtins::kInterpreterPushArgsThenConstructWithFinalSpread: return 190;
  case Builtins::kInterpreterEnterBytecodeAdvance: return 191;
  case Builtins::kInterpreterEnterBytecodeDispatch: return 192;
  case Builtins::kInterpreterOnStackReplacement: return 193;
  case Builtins::kCompileLazy: return 194;
  case Builtins::kCompileLazyDeoptimizedCode: return 195;
  case Builtins::kInstantiateAsmJs: return 196;
  case Builtins::kNotifyDeoptimized: return 197;
  case Builtins::kContinueToCodeStubBuiltin: return 198;
  case Builtins::kContinueToCodeStubBuiltinWithResult: return 199;
  case Builtins::kContinueToJavaScriptBuiltin: return 200;
  case Builtins::kContinueToJavaScriptBuiltinWithResult: return 201;
  case Builtins::kHandleApiCall: return 202;
  case Builtins::kHandleApiCallAsFunction: return 203;
  case Builtins::kHandleApiCallAsConstructor: return 204;
  case Builtins::kAllocateInNewSpace: return 205;
  case Builtins::kAllocateInOldSpace: return 206;
  case Builtins::kCopyFastSmiOrObjectElements: return 207;
  case Builtins::kGrowFastDoubleElements: return 208;
  case Builtins::kGrowFastSmiOrObjectElements: return 209;
  case Builtins::kNewArgumentsElements: return 210;
  case Builtins::kDebugBreakTrampoline: return 211;
  case Builtins::kFrameDropperTrampoline: return 212;
  case Builtins::kHandleDebuggerStatement: return 213;
  case Builtins::kToObject: return 214;
  case Builtins::kToBoolean: return 215;
  case Builtins::kOrdinaryToPrimitive_Number: return 216;
  case Builtins::kOrdinaryToPrimitive_String: return 217;
  case Builtins::kNonPrimitiveToPrimitive_Default: return 218;
  case Builtins::kNonPrimitiveToPrimitive_Number: return 219;
  case Builtins::kNonPrimitiveToPrimitive_String: return 220;
  case Builtins::kStringToNumber: return 221;
  case Builtins::kToName: return 222;
  case Builtins::kNonNumberToNumber: return 223;
  case Builtins::kNonNumberToNumeric: return 224;
  case Builtins::kToNumber: return 225;
  case Builtins::kToNumberConvertBigInt: return 226;
  case Builtins::kToNumeric: return 227;
  case Builtins::kNumberToString: return 228;
  case Builtins::kToString: return 229;
  case Builtins::kToInteger: return 230;
  case Builtins::kToInteger_TruncateMinusZero: return 231;
  case Builtins::kToLength: return 232;
  case Builtins::kTypeof: return 233;
  case Builtins::kGetSuperConstructor: return 234;
  case Builtins::kToBooleanLazyDeoptContinuation: return 235;
  case Builtins::kKeyedLoadIC_PolymorphicName: return 236;
  case Builtins::kKeyedLoadIC_Slow: return 237;
  case Builtins::kKeyedStoreIC_Megamorphic: return 238;
  case Builtins::kKeyedStoreIC_Slow: return 239;
  case Builtins::kLoadGlobalIC_Slow: return 240;
  case Builtins::kLoadIC_FunctionPrototype: return 241;
  case Builtins::kLoadIC_Slow: return 242;
  case Builtins::kLoadIC_StringLength: return 243;
  case Builtins::kLoadIC_StringWrapperLength: return 244;
  case Builtins::kLoadIC_Uninitialized: return 245;
  case Builtins::kStoreGlobalIC_Slow: return 246;
  case Builtins::kStoreIC_Uninitialized: return 247;
  case Builtins::kStoreInArrayLiteralIC_Slow: return 248;
  case Builtins::kEnqueueMicrotask: return 249;
  case Builtins::kRunMicrotasks: return 250;
  case Builtins::kHasProperty: return 251;
  case Builtins::kDeleteProperty: return 252;
  case Builtins::kAbort: return 253;
  case Builtins::kAbortJS: return 254;
  case Builtins::kEmptyFunction: return 255;
  case Builtins::kIllegal: return 256;
  case Builtins::kStrictPoisonPillThrower: return 257;
  case Builtins::kUnsupportedThrower: return 258;
  case Builtins::kReturnReceiver: return 259;
  case Builtins::kArrayConstructor: return 260;
  case Builtins::kArrayConstructorImpl: return 261;
  case Builtins::kArrayNoArgumentConstructor_PackedSmi_DontOverride: return 262;
  case Builtins::kArrayNoArgumentConstructor_HoleySmi_DontOverride: return 263;
  case Builtins::kArrayNoArgumentConstructor_PackedSmi_DisableAllocationSites: return 264;
  case Builtins::kArrayNoArgumentConstructor_HoleySmi_DisableAllocationSites: return 265;
  case Builtins::kArrayNoArgumentConstructor_Packed_DisableAllocationSites: return 266;
  case Builtins::kArrayNoArgumentConstructor_Holey_DisableAllocationSites: return 267;
  case Builtins::kArrayNoArgumentConstructor_PackedDouble_DisableAllocationSites: return 268;
  case Builtins::kArrayNoArgumentConstructor_HoleyDouble_DisableAllocationSites: return 269;
  case Builtins::kArraySingleArgumentConstructor_PackedSmi_DontOverride: return 270;
  case Builtins::kArraySingleArgumentConstructor_HoleySmi_DontOverride: return 271;
  case Builtins::kArraySingleArgumentConstructor_PackedSmi_DisableAllocationSites: return 272;
  case Builtins::kArraySingleArgumentConstructor_HoleySmi_DisableAllocationSites: return 273;
  case Builtins::kArraySingleArgumentConstructor_Packed_DisableAllocationSites: return 274;
  case Builtins::kArraySingleArgumentConstructor_Holey_DisableAllocationSites: return 275;
  case Builtins::kArraySingleArgumentConstructor_PackedDouble_DisableAllocationSites: return 276;
  case Builtins::kArraySingleArgumentConstructor_HoleyDouble_DisableAllocationSites: return 277;
  case Builtins::kArrayNArgumentsConstructor: return 278;
  case Builtins::kInternalArrayConstructor: return 279;
  case Builtins::kInternalArrayConstructorImpl: return 280;
  case Builtins::kInternalArrayNoArgumentConstructor_Packed: return 281;
  case Builtins::kInternalArrayNoArgumentConstructor_Holey: return 282;
  case Builtins::kInternalArraySingleArgumentConstructor_Packed: return 283;
  case Builtins::kInternalArraySingleArgumentConstructor_Holey: return 284;
  case Builtins::kArrayConcat: return 285;
  case Builtins::kArrayIsArray: return 286;
  case Builtins::kArrayPrototypeFill: return 287;
  case Builtins::kArrayFrom: return 288;
  case Builtins::kArrayIncludesSmiOrObject: return 289;
  case Builtins::kArrayIncludesPackedDoubles: return 290;
  case Builtins::kArrayIncludesHoleyDoubles: return 291;
  case Builtins::kArrayIncludes: return 292;
  case Builtins::kArrayIndexOfSmiOrObject: return 293;
  case Builtins::kArrayIndexOfPackedDoubles: return 294;
  case Builtins::kArrayIndexOfHoleyDoubles: return 295;
  case Builtins::kArrayIndexOf: return 296;
  case Builtins::kArrayPop: return 297;
  case Builtins::kArrayPrototypePop: return 298;
  case Builtins::kArrayPush: return 299;
  case Builtins::kArrayPrototypePush: return 300;
  case Builtins::kArrayShift: return 301;
  case Builtins::kArrayPrototypeShift: return 302;
  case Builtins::kArrayPrototypeSlice: return 303;
  case Builtins::kArrayUnshift: return 304;
  case Builtins::kCloneFastJSArray: return 305;
  case Builtins::kCloneFastJSArrayFillingHoles: return 306;
  case Builtins::kExtractFastJSArray: return 307;
  case Builtins::kArrayEveryLoopContinuation: return 308;
  case Builtins::kArrayEveryLoopEagerDeoptContinuation: return 309;
  case Builtins::kArrayEveryLoopLazyDeoptContinuation: return 310;
  case Builtins::kArrayEvery: return 311;
  case Builtins::kArraySomeLoopContinuation: return 312;
  case Builtins::kArraySomeLoopEagerDeoptContinuation: return 313;
  case Builtins::kArraySomeLoopLazyDeoptContinuation: return 314;
  case Builtins::kArraySome: return 315;
  case Builtins::kArrayFilterLoopContinuation: return 316;
  case Builtins::kArrayFilter: return 317;
  case Builtins::kArrayFilterLoopEagerDeoptContinuation: return 318;
  case Builtins::kArrayFilterLoopLazyDeoptContinuation: return 319;
  case Builtins::kArrayMapLoopContinuation: return 320;
  case Builtins::kArrayMapLoopEagerDeoptContinuation: return 321;
  case Builtins::kArrayMapLoopLazyDeoptContinuation: return 322;
  case Builtins::kArrayMap: return 323;
  case Builtins::kArrayReduceLoopContinuation: return 324;
  case Builtins::kArrayReducePreLoopEagerDeoptContinuation: return 325;
  case Builtins::kArrayReduceLoopEagerDeoptContinuation: return 326;
  case Builtins::kArrayReduceLoopLazyDeoptContinuation: return 327;
  case Builtins::kArrayReduce: return 328;
  case Builtins::kArrayReduceRightLoopContinuation: return 329;
  case Builtins::kArrayReduceRightPreLoopEagerDeoptContinuation: return 330;
  case Builtins::kArrayReduceRightLoopEagerDeoptContinuation: return 331;
  case Builtins::kArrayReduceRightLoopLazyDeoptContinuation: return 332;
  case Builtins::kArrayReduceRight: return 333;
  case Builtins::kArrayPrototypeEntries: return 334;
  case Builtins::kArrayFindLoopContinuation: return 335;
  case Builtins::kArrayFindLoopEagerDeoptContinuation: return 336;
  case Builtins::kArrayFindLoopLazyDeoptContinuation: return 337;
  case Builtins::kArrayFindLoopAfterCallbackLazyDeoptContinuation: return 338;
  case Builtins::kArrayPrototypeFind: return 339;
  case Builtins::kArrayFindIndexLoopContinuation: return 340;
  case Builtins::kArrayFindIndexLoopEagerDeoptContinuation: return 341;
  case Builtins::kArrayFindIndexLoopLazyDeoptContinuation: return 342;
  case Builtins::kArrayFindIndexLoopAfterCallbackLazyDeoptContinuation: return 343;
  case Builtins::kArrayPrototypeFindIndex: return 344;
  case Builtins::kArrayPrototypeKeys: return 345;
  case Builtins::kArrayPrototypeValues: return 346;
  case Builtins::kArrayIteratorPrototypeNext: return 347;
  case Builtins::kFlattenIntoArray: return 348;
  case Builtins::kFlatMapIntoArray: return 349;
  case Builtins::kArrayPrototypeFlat: return 350;
  case Builtins::kArrayPrototypeFlatMap: return 351;
  case Builtins::kArrayBufferConstructor: return 352;
  case Builtins::kArrayBufferConstructor_DoNotInitialize: return 353;
  case Builtins::kArrayBufferPrototypeGetByteLength: return 354;
  case Builtins::kArrayBufferIsView: return 355;
  case Builtins::kArrayBufferPrototypeSlice: return 356;
  case Builtins::kAsyncFunctionEnter: return 357;
  case Builtins::kAsyncFunctionReject: return 358;
  case Builtins::kAsyncFunctionResolve: return 359;
  case Builtins::kAsyncFunctionLazyDeoptContinuation: return 360;
  case Builtins::kAsyncFunctionAwaitCaught: return 361;
  case Builtins::kAsyncFunctionAwaitUncaught: return 362;
  case Builtins::kAsyncFunctionAwaitRejectClosure: return 363;
  case Builtins::kAsyncFunctionAwaitResolveClosure: return 364;
  case Builtins::kBigIntConstructor: return 365;
  case Builtins::kBigIntAsUintN: return 366;
  case Builtins::kBigIntAsIntN: return 367;
  case Builtins::kBigIntPrototypeToLocaleString: return 368;
  case Builtins::kBigIntPrototypeToString: return 369;
  case Builtins::kBigIntPrototypeValueOf: return 370;
  case Builtins::kBooleanConstructor: return 371;
  case Builtins::kBooleanPrototypeToString: return 372;
  case Builtins::kBooleanPrototypeValueOf: return 373;
  case Builtins::kCallSitePrototypeGetColumnNumber: return 374;
  case Builtins::kCallSitePrototypeGetEvalOrigin: return 375;
  case Builtins::kCallSitePrototypeGetFileName: return 376;
  case Builtins::kCallSitePrototypeGetFunction: return 377;
  case Builtins::kCallSitePrototypeGetFunctionName: return 378;
  case Builtins::kCallSitePrototypeGetLineNumber: return 379;
  case Builtins::kCallSitePrototypeGetMethodName: return 380;
  case Builtins::kCallSitePrototypeGetPosition: return 381;
  case Builtins::kCallSitePrototypeGetPromiseIndex: return 382;
  case Builtins::kCallSitePrototypeGetScriptNameOrSourceURL: return 383;
  case Builtins::kCallSitePrototypeGetThis: return 384;
  case Builtins::kCallSitePrototypeGetTypeName: return 385;
  case Builtins::kCallSitePrototypeIsAsync: return 386;
  case Builtins::kCallSitePrototypeIsConstructor: return 387;
  case Builtins::kCallSitePrototypeIsEval: return 388;
  case Builtins::kCallSitePrototypeIsNative: return 389;
  case Builtins::kCallSitePrototypeIsPromiseAll: return 390;
  case Builtins::kCallSitePrototypeIsToplevel: return 391;
  case Builtins::kCallSitePrototypeToString: return 392;
  case Builtins::kConsoleDebug: return 393;
  case Builtins::kConsoleError: return 394;
  case Builtins::kConsoleInfo: return 395;
  case Builtins::kConsoleLog: return 396;
  case Builtins::kConsoleWarn: return 397;
  case Builtins::kConsoleDir: return 398;
  case Builtins::kConsoleDirXml: return 399;
  case Builtins::kConsoleTable: return 400;
  case Builtins::kConsoleTrace: return 401;
  case Builtins::kConsoleGroup: return 402;
  case Builtins::kConsoleGroupCollapsed: return 403;
  case Builtins::kConsoleGroupEnd: return 404;
  case Builtins::kConsoleClear: return 405;
  case Builtins::kConsoleCount: return 406;
  case Builtins::kConsoleCountReset: return 407;
  case Builtins::kConsoleAssert: return 408;
  case Builtins::kFastConsoleAssert: return 409;
  case Builtins::kConsoleProfile: return 410;
  case Builtins::kConsoleProfileEnd: return 411;
  case Builtins::kConsoleTime: return 412;
  case Builtins::kConsoleTimeLog: return 413;
  case Builtins::kConsoleTimeEnd: return 414;
  case Builtins::kConsoleTimeStamp: return 415;
  case Builtins::kConsoleContext: return 416;
  case Builtins::kDataViewConstructor: return 417;
  case Builtins::kDateConstructor: return 418;
  case Builtins::kDatePrototypeGetDate: return 419;
  case Builtins::kDatePrototypeGetDay: return 420;
  case Builtins::kDatePrototypeGetFullYear: return 421;
  case Builtins::kDatePrototypeGetHours: return 422;
  case Builtins::kDatePrototypeGetMilliseconds: return 423;
  case Builtins::kDatePrototypeGetMinutes: return 424;
  case Builtins::kDatePrototypeGetMonth: return 425;
  case Builtins::kDatePrototypeGetSeconds: return 426;
  case Builtins::kDatePrototypeGetTime: return 427;
  case Builtins::kDatePrototypeGetTimezoneOffset: return 428;
  case Builtins::kDatePrototypeGetUTCDate: return 429;
  case Builtins::kDatePrototypeGetUTCDay: return 430;
  case Builtins::kDatePrototypeGetUTCFullYear: return 431;
  case Builtins::kDatePrototypeGetUTCHours: return 432;
  case Builtins::kDatePrototypeGetUTCMilliseconds: return 433;
  case Builtins::kDatePrototypeGetUTCMinutes: return 434;
  case Builtins::kDatePrototypeGetUTCMonth: return 435;
  case Builtins::kDatePrototypeGetUTCSeconds: return 436;
  case Builtins::kDatePrototypeValueOf: return 437;
  case Builtins::kDatePrototypeToPrimitive: return 438;
  case Builtins::kDatePrototypeGetYear: return 439;
  case Builtins::kDatePrototypeSetYear: return 440;
  case Builtins::kDateNow: return 441;
  case Builtins::kDateParse: return 442;
  case Builtins::kDatePrototypeSetDate: return 443;
  case Builtins::kDatePrototypeSetFullYear: return 444;
  case Builtins::kDatePrototypeSetHours: return 445;
  case Builtins::kDatePrototypeSetMilliseconds: return 446;
  case Builtins::kDatePrototypeSetMinutes: return 447;
  case Builtins::kDatePrototypeSetMonth: return 448;
  case Builtins::kDatePrototypeSetSeconds: return 449;
  case Builtins::kDatePrototypeSetTime: return 450;
  case Builtins::kDatePrototypeSetUTCDate: return 451;
  case Builtins::kDatePrototypeSetUTCFullYear: return 452;
  case Builtins::kDatePrototypeSetUTCHours: return 453;
  case Builtins::kDatePrototypeSetUTCMilliseconds: return 454;
  case Builtins::kDatePrototypeSetUTCMinutes: return 455;
  case Builtins::kDatePrototypeSetUTCMonth: return 456;
  case Builtins::kDatePrototypeSetUTCSeconds: return 457;
  case Builtins::kDatePrototypeToDateString: return 458;
  case Builtins::kDatePrototypeToISOString: return 459;
  case Builtins::kDatePrototypeToUTCString: return 460;
  case Builtins::kDatePrototypeToString: return 461;
  case Builtins::kDatePrototypeToTimeString: return 462;
  case Builtins::kDatePrototypeToJson: return 463;
  case Builtins::kDateUTC: return 464;
  case Builtins::kErrorConstructor: return 465;
  case Builtins::kErrorCaptureStackTrace: return 466;
  case Builtins::kErrorPrototypeToString: return 467;
  case Builtins::kMakeError: return 468;
  case Builtins::kMakeRangeError: return 469;
  case Builtins::kMakeSyntaxError: return 470;
  case Builtins::kMakeTypeError: return 471;
  case Builtins::kMakeURIError: return 472;
  case Builtins::kFunctionConstructor: return 473;
  case Builtins::kFunctionPrototypeApply: return 474;
  case Builtins::kFunctionPrototypeBind: return 475;
  case Builtins::kFastFunctionPrototypeBind: return 476;
  case Builtins::kFunctionPrototypeCall: return 477;
  case Builtins::kFunctionPrototypeHasInstance: return 478;
  case Builtins::kFunctionPrototypeToString: return 479;
  case Builtins::kCreateIterResultObject: return 480;
  case Builtins::kCreateGeneratorObject: return 481;
  case Builtins::kGeneratorFunctionConstructor: return 482;
  case Builtins::kGeneratorPrototypeNext: return 483;
  case Builtins::kGeneratorPrototypeReturn: return 484;
  case Builtins::kGeneratorPrototypeThrow: return 485;
  case Builtins::kAsyncFunctionConstructor: return 486;
  case Builtins::kGlobalDecodeURI: return 487;
  case Builtins::kGlobalDecodeURIComponent: return 488;
  case Builtins::kGlobalEncodeURI: return 489;
  case Builtins::kGlobalEncodeURIComponent: return 490;
  case Builtins::kGlobalEscape: return 491;
  case Builtins::kGlobalUnescape: return 492;
  case Builtins::kGlobalEval: return 493;
  case Builtins::kGlobalIsFinite: return 494;
  case Builtins::kGlobalIsNaN: return 495;
  case Builtins::kJsonParse: return 496;
  case Builtins::kJsonStringify: return 497;
  case Builtins::kLoadIC: return 498;
  case Builtins::kLoadIC_Megamorphic: return 499;
  case Builtins::kLoadIC_Noninlined: return 500;
  case Builtins::kLoadICTrampoline: return 501;
  case Builtins::kLoadICTrampoline_Megamorphic: return 502;
  case Builtins::kKeyedLoadIC: return 503;
  case Builtins::kKeyedLoadIC_Megamorphic: return 504;
  case Builtins::kKeyedLoadICTrampoline: return 505;
  case Builtins::kKeyedLoadICTrampoline_Megamorphic: return 506;
  case Builtins::kStoreGlobalIC: return 507;
  case Builtins::kStoreGlobalICTrampoline: return 508;
  case Builtins::kStoreIC: return 509;
  case Builtins::kStoreICTrampoline: return 510;
  case Builtins::kKeyedStoreIC: return 511;
  case Builtins::kKeyedStoreICTrampoline: return 512;
  case Builtins::kStoreInArrayLiteralIC: return 513;
  case Builtins::kLoadGlobalIC: return 514;
  case Builtins::kLoadGlobalICInsideTypeof: return 515;
  case Builtins::kLoadGlobalICTrampoline: return 516;
  case Builtins::kLoadGlobalICInsideTypeofTrampoline: return 517;
  case Builtins::kCloneObjectIC: return 518;
  case Builtins::kCloneObjectIC_Slow: return 519;
  case Builtins::kIterableToList: return 520;
  case Builtins::kIterableToListWithSymbolLookup: return 521;
  case Builtins::kIterableToListMayPreserveHoles: return 522;
  case Builtins::kFindOrderedHashMapEntry: return 523;
  case Builtins::kMapConstructor: return 524;
  case Builtins::kMapPrototypeSet: return 525;
  case Builtins::kMapPrototypeDelete: return 526;
  case Builtins::kMapPrototypeGet: return 527;
  case Builtins::kMapPrototypeHas: return 528;
  case Builtins::kMapPrototypeClear: return 529;
  case Builtins::kMapPrototypeEntries: return 530;
  case Builtins::kMapPrototypeGetSize: return 531;
  case Builtins::kMapPrototypeForEach: return 532;
  case Builtins::kMapPrototypeKeys: return 533;
  case Builtins::kMapPrototypeValues: return 534;
  case Builtins::kMapIteratorPrototypeNext: return 535;
  case Builtins::kMapIteratorToList: return 536;
  case Builtins::kMathAbs: return 537;
  case Builtins::kMathAcos: return 538;
  case Builtins::kMathAcosh: return 539;
  case Builtins::kMathAsin: return 540;
  case Builtins::kMathAsinh: return 541;
  case Builtins::kMathAtan: return 542;
  case Builtins::kMathAtanh: return 543;
  case Builtins::kMathAtan2: return 544;
  case Builtins::kMathCbrt: return 545;
  case Builtins::kMathCeil: return 546;
  case Builtins::kMathClz32: return 547;
  case Builtins::kMathCos: return 548;
  case Builtins::kMathCosh: return 549;
  case Builtins::kMathExp: return 550;
  case Builtins::kMathExpm1: return 551;
  case Builtins::kMathFloor: return 552;
  case Builtins::kMathFround: return 553;
  case Builtins::kMathHypot: return 554;
  case Builtins::kMathImul: return 555;
  case Builtins::kMathLog: return 556;
  case Builtins::kMathLog1p: return 557;
  case Builtins::kMathLog10: return 558;
  case Builtins::kMathLog2: return 559;
  case Builtins::kMathMax: return 560;
  case Builtins::kMathMin: return 561;
  case Builtins::kMathPow: return 562;
  case Builtins::kMathRandom: return 563;
  case Builtins::kMathRound: return 564;
  case Builtins::kMathSign: return 565;
  case Builtins::kMathSin: return 566;
  case Builtins::kMathSinh: return 567;
  case Builtins::kMathTan: return 568;
  case Builtins::kMathTanh: return 569;
  case Builtins::kMathSqrt: return 570;
  case Builtins::kMathTrunc: return 571;
  case Builtins::kAllocateHeapNumber: return 572;
  case Builtins::kNumberConstructor: return 573;
  case Builtins::kNumberIsFinite: return 574;
  case Builtins::kNumberIsInteger: return 575;
  case Builtins::kNumberIsNaN: return 576;
  case Builtins::kNumberIsSafeInteger: return 577;
  case Builtins::kNumberParseFloat: return 578;
  case Builtins::kNumberParseInt: return 579;
  case Builtins::kParseInt: return 580;
  case Builtins::kNumberPrototypeToExponential: return 581;
  case Builtins::kNumberPrototypeToFixed: return 582;
  case Builtins::kNumberPrototypeToLocaleString: return 583;
  case Builtins::kNumberPrototypeToPrecision: return 584;
  case Builtins::kNumberPrototypeToString: return 585;
  case Builtins::kNumberPrototypeValueOf: return 586;
  case Builtins::kAdd: return 587;
  case Builtins::kSubtract: return 588;
  case Builtins::kMultiply: return 589;
  case Builtins::kDivide: return 590;
  case Builtins::kModulus: return 591;
  case Builtins::kExponentiate: return 592;
  case Builtins::kBitwiseAnd: return 593;
  case Builtins::kBitwiseOr: return 594;
  case Builtins::kBitwiseXor: return 595;
  case Builtins::kShiftLeft: return 596;
  case Builtins::kShiftRight: return 597;
  case Builtins::kShiftRightLogical: return 598;
  case Builtins::kLessThan: return 599;
  case Builtins::kLessThanOrEqual: return 600;
  case Builtins::kGreaterThan: return 601;
  case Builtins::kGreaterThanOrEqual: return 602;
  case Builtins::kEqual: return 603;
  case Builtins::kSameValue: return 604;
  case Builtins::kStrictEqual: return 605;
  case Builtins::kBitwiseNot: return 606;
  case Builtins::kDecrement: return 607;
  case Builtins::kIncrement: return 608;
  case Builtins::kNegate: return 609;
  case Builtins::kObjectConstructor: return 610;
  case Builtins::kObjectAssign: return 611;
  case Builtins::kObjectCreate: return 612;
  case Builtins::kCreateObjectWithoutProperties: return 613;
  case Builtins::kObjectDefineGetter: return 614;
  case Builtins::kObjectDefineProperties: return 615;
  case Builtins::kObjectDefineProperty: return 616;
  case Builtins::kObjectDefineSetter: return 617;
  case Builtins::kObjectEntries: return 618;
  case Builtins::kObjectFreeze: return 619;
  case Builtins::kObjectGetOwnPropertyDescriptor: return 620;
  case Builtins::kObjectGetOwnPropertyDescriptors: return 621;
  case Builtins::kObjectGetOwnPropertyNames: return 622;
  case Builtins::kObjectGetOwnPropertySymbols: return 623;
  case Builtins::kObjectGetPrototypeOf: return 624;
  case Builtins::kObjectSetPrototypeOf: return 625;
  case Builtins::kObjectIs: return 626;
  case Builtins::kObjectIsExtensible: return 627;
  case Builtins::kObjectIsFrozen: return 628;
  case Builtins::kObjectIsSealed: return 629;
  case Builtins::kObjectKeys: return 630;
  case Builtins::kObjectLookupGetter: return 631;
  case Builtins::kObjectLookupSetter: return 632;
  case Builtins::kObjectPreventExtensions: return 633;
  case Builtins::kObjectPrototypeToString: return 634;
  case Builtins::kObjectPrototypeValueOf: return 635;
  case Builtins::kObjectPrototypeHasOwnProperty: return 636;
  case Builtins::kObjectPrototypeIsPrototypeOf: return 637;
  case Builtins::kObjectPrototypePropertyIsEnumerable: return 638;
  case Builtins::kObjectPrototypeGetProto: return 639;
  case Builtins::kObjectPrototypeSetProto: return 640;
  case Builtins::kObjectPrototypeToLocaleString: return 641;
  case Builtins::kObjectSeal: return 642;
  case Builtins::kObjectToString: return 643;
  case Builtins::kObjectValues: return 644;
  case Builtins::kOrdinaryHasInstance: return 645;
  case Builtins::kInstanceOf: return 646;
  case Builtins::kForInEnumerate: return 647;
  case Builtins::kForInFilter: return 648;
  case Builtins::kFulfillPromise: return 649;
  case Builtins::kRejectPromise: return 650;
  case Builtins::kResolvePromise: return 651;
  case Builtins::kPromiseCapabilityDefaultReject: return 652;
  case Builtins::kPromiseCapabilityDefaultResolve: return 653;
  case Builtins::kPromiseGetCapabilitiesExecutor: return 654;
  case Builtins::kNewPromiseCapability: return 655;
  case Builtins::kPromiseConstructorLazyDeoptContinuation: return 656;
  case Builtins::kPromiseConstructor: return 657;
  case Builtins::kIsPromise: return 658;
  case Builtins::kPromisePrototypeThen: return 659;
  case Builtins::kPerformPromiseThen: return 660;
  case Builtins::kPromisePrototypeCatch: return 661;
  case Builtins::kPromiseRejectReactionJob: return 662;
  case Builtins::kPromiseFulfillReactionJob: return 663;
  case Builtins::kPromiseResolveThenableJob: return 664;
  case Builtins::kPromiseResolveTrampoline: return 665;
  case Builtins::kPromiseResolve: return 666;
  case Builtins::kPromiseReject: return 667;
  case Builtins::kPromisePrototypeFinally: return 668;
  case Builtins::kPromiseThenFinally: return 669;
  case Builtins::kPromiseCatchFinally: return 670;
  case Builtins::kPromiseValueThunkFinally: return 671;
  case Builtins::kPromiseThrowerFinally: return 672;
  case Builtins::kPromiseAll: return 673;
  case Builtins::kPromiseAllResolveElementClosure: return 674;
  case Builtins::kPromiseRace: return 675;
  case Builtins::kPromiseInternalConstructor: return 676;
  case Builtins::kPromiseInternalReject: return 677;
  case Builtins::kPromiseInternalResolve: return 678;
  case Builtins::kProxyConstructor: return 679;
  case Builtins::kProxyRevocable: return 680;
  case Builtins::kProxyRevoke: return 681;
  case Builtins::kProxyGetProperty: return 682;
  case Builtins::kProxyHasProperty: return 683;
  case Builtins::kProxySetProperty: return 684;
  case Builtins::kReflectApply: return 685;
  case Builtins::kReflectConstruct: return 686;
  case Builtins::kReflectDefineProperty: return 687;
  case Builtins::kReflectDeleteProperty: return 688;
  case Builtins::kReflectGet: return 689;
  case Builtins::kReflectGetOwnPropertyDescriptor: return 690;
  case Builtins::kReflectGetPrototypeOf: return 691;
  case Builtins::kReflectHas: return 692;
  case Builtins::kReflectIsExtensible: return 693;
  case Builtins::kReflectOwnKeys: return 694;
  case Builtins::kReflectPreventExtensions: return 695;
  case Builtins::kReflectSet: return 696;
  case Builtins::kReflectSetPrototypeOf: return 697;
  case Builtins::kRegExpCapture1Getter: return 698;
  case Builtins::kRegExpCapture2Getter: return 699;
  case Builtins::kRegExpCapture3Getter: return 700;
  case Builtins::kRegExpCapture4Getter: return 701;
  case Builtins::kRegExpCapture5Getter: return 702;
  case Builtins::kRegExpCapture6Getter: return 703;
  case Builtins::kRegExpCapture7Getter: return 704;
  case Builtins::kRegExpCapture8Getter: return 705;
  case Builtins::kRegExpCapture9Getter: return 706;
  case Builtins::kRegExpConstructor: return 707;
  case Builtins::kRegExpInternalMatch: return 708;
  case Builtins::kRegExpInputGetter: return 709;
  case Builtins::kRegExpInputSetter: return 710;
  case Builtins::kRegExpLastMatchGetter: return 711;
  case Builtins::kRegExpLastParenGetter: return 712;
  case Builtins::kRegExpLeftContextGetter: return 713;
  case Builtins::kRegExpPrototypeCompile: return 714;
  case Builtins::kRegExpPrototypeExec: return 715;
  case Builtins::kRegExpPrototypeDotAllGetter: return 716;
  case Builtins::kRegExpPrototypeFlagsGetter: return 717;
  case Builtins::kRegExpPrototypeGlobalGetter: return 718;
  case Builtins::kRegExpPrototypeIgnoreCaseGetter: return 719;
  case Builtins::kRegExpPrototypeMatch: return 720;
  case Builtins::kRegExpPrototypeMatchAll: return 721;
  case Builtins::kRegExpPrototypeMultilineGetter: return 722;
  case Builtins::kRegExpPrototypeSearch: return 723;
  case Builtins::kRegExpPrototypeSourceGetter: return 724;
  case Builtins::kRegExpPrototypeStickyGetter: return 725;
  case Builtins::kRegExpPrototypeTest: return 726;
  case Builtins::kRegExpPrototypeTestFast: return 727;
  case Builtins::kRegExpPrototypeToString: return 728;
  case Builtins::kRegExpPrototypeUnicodeGetter: return 729;
  case Builtins::kRegExpRightContextGetter: return 730;
  case Builtins::kRegExpPrototypeReplace: return 731;
  case Builtins::kRegExpPrototypeSplit: return 732;
  case Builtins::kRegExpExecAtom: return 733;
  case Builtins::kRegExpExecInternal: return 734;
  case Builtins::kRegExpMatchFast: return 735;
  case Builtins::kRegExpPrototypeExecSlow: return 736;
  case Builtins::kRegExpReplace: return 737;
  case Builtins::kRegExpSearchFast: return 738;
  case Builtins::kRegExpSplit: return 739;
  case Builtins::kRegExpStringIteratorPrototypeNext: return 740;
  case Builtins::kSetConstructor: return 741;
  case Builtins::kSetPrototypeHas: return 742;
  case Builtins::kSetPrototypeAdd: return 743;
  case Builtins::kSetPrototypeDelete: return 744;
  case Builtins::kSetPrototypeClear: return 745;
  case Builtins::kSetPrototypeEntries: return 746;
  case Builtins::kSetPrototypeGetSize: return 747;
  case Builtins::kSetPrototypeForEach: return 748;
  case Builtins::kSetPrototypeValues: return 749;
  case Builtins::kSetIteratorPrototypeNext: return 750;
  case Builtins::kSetOrSetIteratorToList: return 751;
  case Builtins::kSharedArrayBufferPrototypeGetByteLength: return 752;
  case Builtins::kSharedArrayBufferPrototypeSlice: return 753;
  case Builtins::kAtomicsLoad: return 754;
  case Builtins::kAtomicsStore: return 755;
  case Builtins::kAtomicsExchange: return 756;
  case Builtins::kAtomicsCompareExchange: return 757;
  case Builtins::kAtomicsAdd: return 758;
  case Builtins::kAtomicsSub: return 759;
  case Builtins::kAtomicsAnd: return 760;
  case Builtins::kAtomicsOr: return 761;
  case Builtins::kAtomicsXor: return 762;
  case Builtins::kAtomicsNotify: return 763;
  case Builtins::kAtomicsIsLockFree: return 764;
  case Builtins::kAtomicsWait: return 765;
  case Builtins::kAtomicsWake: return 766;
  case Builtins::kStringConstructor: return 767;
  case Builtins::kStringFromCodePoint: return 768;
  case Builtins::kStringFromCharCode: return 769;
  case Builtins::kStringPrototypeAnchor: return 770;
  case Builtins::kStringPrototypeBig: return 771;
  case Builtins::kStringPrototypeBlink: return 772;
  case Builtins::kStringPrototypeBold: return 773;
  case Builtins::kStringPrototypeCharAt: return 774;
  case Builtins::kStringPrototypeCharCodeAt: return 775;
  case Builtins::kStringPrototypeCodePointAt: return 776;
  case Builtins::kStringPrototypeConcat: return 777;
  case Builtins::kStringPrototypeEndsWith: return 778;
  case Builtins::kStringPrototypeFontcolor: return 779;
  case Builtins::kStringPrototypeFontsize: return 780;
  case Builtins::kStringPrototypeFixed: return 781;
  case Builtins::kStringPrototypeIncludes: return 782;
  case Builtins::kStringPrototypeIndexOf: return 783;
  case Builtins::kStringPrototypeItalics: return 784;
  case Builtins::kStringPrototypeLastIndexOf: return 785;
  case Builtins::kStringPrototypeLink: return 786;
  case Builtins::kStringPrototypeMatch: return 787;
  case Builtins::kStringPrototypeMatchAll: return 788;
  case Builtins::kStringPrototypeLocaleCompare: return 789;
  case Builtins::kStringPrototypePadEnd: return 790;
  case Builtins::kStringPrototypePadStart: return 791;
  case Builtins::kStringPrototypeRepeat: return 792;
  case Builtins::kStringPrototypeReplace: return 793;
  case Builtins::kStringPrototypeSearch: return 794;
  case Builtins::kStringPrototypeSlice: return 795;
  case Builtins::kStringPrototypeSmall: return 796;
  case Builtins::kStringPrototypeSplit: return 797;
  case Builtins::kStringPrototypeStrike: return 798;
  case Builtins::kStringPrototypeSub: return 799;
  case Builtins::kStringPrototypeSubstr: return 800;
  case Builtins::kStringPrototypeSubstring: return 801;
  case Builtins::kStringPrototypeSup: return 802;
  case Builtins::kStringPrototypeStartsWith: return 803;
  case Builtins::kStringPrototypeToString: return 804;
  case Builtins::kStringPrototypeTrim: return 805;
  case Builtins::kStringPrototypeTrimEnd: return 806;
  case Builtins::kStringPrototypeTrimStart: return 807;
  case Builtins::kStringPrototypeValueOf: return 808;
  case Builtins::kStringRaw: return 809;
  case Builtins::kStringPrototypeIterator: return 810;
  case Builtins::kStringIteratorPrototypeNext: return 811;
  case Builtins::kStringToList: return 812;
  case Builtins::kSymbolConstructor: return 813;
  case Builtins::kSymbolFor: return 814;
  case Builtins::kSymbolKeyFor: return 815;
  case Builtins::kSymbolPrototypeDescriptionGetter: return 816;
  case Builtins::kSymbolPrototypeToPrimitive: return 817;
  case Builtins::kSymbolPrototypeToString: return 818;
  case Builtins::kSymbolPrototypeValueOf: return 819;
  case Builtins::kTypedArrayInitialize: return 820;
  case Builtins::kTypedArrayInitializeWithBuffer: return 821;
  case Builtins::kCreateTypedArray: return 822;
  case Builtins::kTypedArrayBaseConstructor: return 823;
  case Builtins::kGenericConstructorLazyDeoptContinuation: return 824;
  case Builtins::kTypedArrayConstructor: return 825;
  case Builtins::kTypedArrayPrototypeBuffer: return 826;
  case Builtins::kTypedArrayPrototypeByteLength: return 827;
  case Builtins::kTypedArrayPrototypeByteOffset: return 828;
  case Builtins::kTypedArrayPrototypeLength: return 829;
  case Builtins::kTypedArrayPrototypeEntries: return 830;
  case Builtins::kTypedArrayPrototypeKeys: return 831;
  case Builtins::kTypedArrayPrototypeValues: return 832;
  case Builtins::kTypedArrayPrototypeCopyWithin: return 833;
  case Builtins::kTypedArrayPrototypeFill: return 834;
  case Builtins::kTypedArrayPrototypeFilter: return 835;
  case Builtins::kTypedArrayPrototypeFind: return 836;
  case Builtins::kTypedArrayPrototypeFindIndex: return 837;
  case Builtins::kTypedArrayPrototypeIncludes: return 838;
  case Builtins::kTypedArrayPrototypeIndexOf: return 839;
  case Builtins::kTypedArrayPrototypeLastIndexOf: return 840;
  case Builtins::kTypedArrayPrototypeReverse: return 841;
  case Builtins::kTypedArrayPrototypeSet: return 842;
  case Builtins::kTypedArrayPrototypeSlice: return 843;
  case Builtins::kTypedArrayPrototypeSubArray: return 844;
  case Builtins::kTypedArrayPrototypeToStringTag: return 845;
  case Builtins::kTypedArrayPrototypeEvery: return 846;
  case Builtins::kTypedArrayPrototypeSome: return 847;
  case Builtins::kTypedArrayPrototypeReduce: return 848;
  case Builtins::kTypedArrayPrototypeReduceRight: return 849;
  case Builtins::kTypedArrayPrototypeMap: return 850;
  case Builtins::kTypedArrayPrototypeForEach: return 851;
  case Builtins::kTypedArrayOf: return 852;
  case Builtins::kTypedArrayFrom: return 853;
  case Builtins::kWasmCompileLazy: return 854;
  case Builtins::kWasmAllocateHeapNumber: return 855;
  case Builtins::kWasmCallJavaScript: return 856;
  case Builtins::kWasmMemoryGrow: return 857;
  case Builtins::kWasmRecordWrite: return 858;
  case Builtins::kWasmStackGuard: return 859;
  case Builtins::kWasmToNumber: return 860;
  case Builtins::kWasmThrow: return 861;
  case Builtins::kThrowWasmTrapUnreachable: return 862;
  case Builtins::kThrowWasmTrapMemOutOfBounds: return 863;
  case Builtins::kThrowWasmTrapUnalignedAccess: return 864;
  case Builtins::kThrowWasmTrapDivByZero: return 865;
  case Builtins::kThrowWasmTrapDivUnrepresentable: return 866;
  case Builtins::kThrowWasmTrapRemByZero: return 867;
  case Builtins::kThrowWasmTrapFloatUnrepresentable: return 868;
  case Builtins::kThrowWasmTrapFuncInvalid: return 869;
  case Builtins::kThrowWasmTrapFuncSigMismatch: return 870;
  case Builtins::kWeakMapConstructor: return 871;
  case Builtins::kWeakMapLookupHashIndex: return 872;
  case Builtins::kWeakMapGet: return 873;
  case Builtins::kWeakMapHas: return 874;
  case Builtins::kWeakMapPrototypeSet: return 875;
  case Builtins::kWeakMapPrototypeDelete: return 876;
  case Builtins::kWeakSetConstructor: return 877;
  case Builtins::kWeakSetHas: return 878;
  case Builtins::kWeakSetPrototypeAdd: return 879;
  case Builtins::kWeakSetPrototypeDelete: return 880;
  case Builtins::kWeakCollectionDelete: return 881;
  case Builtins::kWeakCollectionSet: return 882;
  case Builtins::kAsyncGeneratorResolve: return 883;
  case Builtins::kAsyncGeneratorReject: return 884;
  case Builtins::kAsyncGeneratorYield: return 885;
  case Builtins::kAsyncGeneratorReturn: return 886;
  case Builtins::kAsyncGeneratorResumeNext: return 887;
  case Builtins::kAsyncGeneratorFunctionConstructor: return 888;
  case Builtins::kAsyncGeneratorPrototypeNext: return 889;
  case Builtins::kAsyncGeneratorPrototypeReturn: return 890;
  case Builtins::kAsyncGeneratorPrototypeThrow: return 891;
  case Builtins::kAsyncGeneratorAwaitCaught: return 892;
  case Builtins::kAsyncGeneratorAwaitUncaught: return 893;
  case Builtins::kAsyncGeneratorAwaitResolveClosure: return 894;
  case Builtins::kAsyncGeneratorAwaitRejectClosure: return 895;
  case Builtins::kAsyncGeneratorYieldResolveClosure: return 896;
  case Builtins::kAsyncGeneratorReturnClosedResolveClosure: return 897;
  case Builtins::kAsyncGeneratorReturnClosedRejectClosure: return 898;
  case Builtins::kAsyncGeneratorReturnResolveClosure: return 899;
  case Builtins::kAsyncFromSyncIteratorPrototypeNext: return 900;
  case Builtins::kAsyncFromSyncIteratorPrototypeThrow: return 901;
  case Builtins::kAsyncFromSyncIteratorPrototypeReturn: return 902;
  case Builtins::kAsyncIteratorValueUnwrap: return 903;
  case Builtins::kCEntry_Return1_DontSaveFPRegs_ArgvOnStack_NoBuiltinExit: return 904;
  case Builtins::kCEntry_Return1_DontSaveFPRegs_ArgvOnStack_BuiltinExit: return 905;
  case Builtins::kCEntry_Return1_DontSaveFPRegs_ArgvInRegister_NoBuiltinExit: return 906;
  case Builtins::kCEntry_Return1_SaveFPRegs_ArgvOnStack_NoBuiltinExit: return 907;
  case Builtins::kCEntry_Return1_SaveFPRegs_ArgvOnStack_BuiltinExit: return 908;
  case Builtins::kCEntry_Return2_DontSaveFPRegs_ArgvOnStack_NoBuiltinExit: return 909;
  case Builtins::kCEntry_Return2_DontSaveFPRegs_ArgvOnStack_BuiltinExit: return 910;
  case Builtins::kCEntry_Return2_DontSaveFPRegs_ArgvInRegister_NoBuiltinExit: return 911;
  case Builtins::kCEntry_Return2_SaveFPRegs_ArgvOnStack_NoBuiltinExit: return 912;
  case Builtins::kCEntry_Return2_SaveFPRegs_ArgvOnStack_BuiltinExit: return 913;
  case Builtins::kStringAdd_CheckNone: return 914;
  case Builtins::kStringAdd_ConvertLeft: return 915;
  case Builtins::kStringAdd_ConvertRight: return 916;
  case Builtins::kSubString: return 917;
  case Builtins::kCallApiCallback_Argc0: return 918;
  case Builtins::kCallApiCallback_Argc1: return 919;
  case Builtins::kCallApiGetter: return 920;
  case Builtins::kDoubleToI: return 921;
  case Builtins::kGetProperty: return 922;
  case Builtins::kSetProperty: return 923;
  case Builtins::kSetPropertyInLiteral: return 924;
  case Builtins::kMathPowInternal: return 925;
  case Builtins::kIsTraceCategoryEnabled: return 926;
  case Builtins::kTrace: return 927;
  case Builtins::kWeakCellClear: return 928;
  case Builtins::kWeakCellHoldingsGetter: return 929;
  case Builtins::kWeakFactoryCleanupIteratorNext: return 930;
  case Builtins::kWeakFactoryConstructor: return 931;
  case Builtins::kWeakFactoryMakeCell: return 932;
  case Builtins::kWeakFactoryMakeRef: return 933;
  case Builtins::kWeakRefDeref: return 934;
  case Builtins::kArrayPrototypeCopyWithin: return 935;
  case Builtins::kArrayForEachLoopEagerDeoptContinuation: return 936;
  case Builtins::kArrayForEachLoopLazyDeoptContinuation: return 937;
  case Builtins::kArrayForEachLoopContinuation: return 938;
  case Builtins::kArrayForEach: return 939;
  case Builtins::kLoadJoinElement20ATDictionaryElements: return 940;
  case Builtins::kLoadJoinElement25ATFastSmiOrObjectElements: return 941;
  case Builtins::kLoadJoinElement20ATFastDoubleElements: return 942;
  case Builtins::kConvertToLocaleString: return 943;
  case Builtins::kArrayJoinWithToLocaleString: return 944;
  case Builtins::kArrayJoinWithoutToLocaleString: return 945;
  case Builtins::kJoinStackPush: return 946;
  case Builtins::kJoinStackPop: return 947;
  case Builtins::kArrayPrototypeJoin: return 948;
  case Builtins::kArrayPrototypeToLocaleString: return 949;
  case Builtins::kArrayPrototypeToString: return 950;
  case Builtins::kArrayPrototypeLastIndexOf: return 951;
  case Builtins::kArrayOf: return 952;
  case Builtins::kArrayPrototypeReverse: return 953;
  case Builtins::kArraySlice: return 954;
  case Builtins::kArraySplice: return 955;
  case Builtins::kArrayPrototypeUnshift: return 956;
  case Builtins::kTypedArrayQuickSort: return 957;
  case Builtins::kTypedArrayPrototypeSort: return 958;
  case Builtins::kDataViewPrototypeGetBuffer: return 959;
  case Builtins::kDataViewPrototypeGetByteLength: return 960;
  case Builtins::kDataViewPrototypeGetByteOffset: return 961;
  case Builtins::kDataViewPrototypeGetUint8: return 962;
  case Builtins::kDataViewPrototypeGetInt8: return 963;
  case Builtins::kDataViewPrototypeGetUint16: return 964;
  case Builtins::kDataViewPrototypeGetInt16: return 965;
  case Builtins::kDataViewPrototypeGetUint32: return 966;
  case Builtins::kDataViewPrototypeGetInt32: return 967;
  case Builtins::kDataViewPrototypeGetFloat32: return 968;
  case Builtins::kDataViewPrototypeGetFloat64: return 969;
  case Builtins::kDataViewPrototypeGetBigUint64: return 970;
  case Builtins::kDataViewPrototypeGetBigInt64: return 971;
  case Builtins::kDataViewPrototypeSetUint8: return 972;
  case Builtins::kDataViewPrototypeSetInt8: return 973;
  case Builtins::kDataViewPrototypeSetUint16: return 974;
  case Builtins::kDataViewPrototypeSetInt16: return 975;
  case Builtins::kDataViewPrototypeSetUint32: return 976;
  case Builtins::kDataViewPrototypeSetInt32: return 977;
  case Builtins::kDataViewPrototypeSetFloat32: return 978;
  case Builtins::kDataViewPrototypeSetFloat64: return 979;
  case Builtins::kDataViewPrototypeSetBigUint64: return 980;
  case Builtins::kDataViewPrototypeSetBigInt64: return 981;
  case Builtins::kGenericBuiltinTest22UT12ATHeapObject5ATSmi: return 982;
  case Builtins::kTestHelperPlus1: return 983;
  case Builtins::kTestHelperPlus2: return 984;
  case Builtins::kLoad23ATFastPackedSmiElements: return 985;
  case Builtins::kLoad25ATFastSmiOrObjectElements: return 986;
  case Builtins::kLoad20ATFastDoubleElements: return 987;
  case Builtins::kLoad20ATDictionaryElements: return 988;
  case Builtins::kLoad19ATTempArrayElements: return 989;
  case Builtins::kStore23ATFastPackedSmiElements: return 990;
  case Builtins::kStore25ATFastSmiOrObjectElements: return 991;
  case Builtins::kStore20ATFastDoubleElements: return 992;
  case Builtins::kStore20ATDictionaryElements: return 993;
  case Builtins::kStore19ATTempArrayElements: return 994;
  case Builtins::kSortCompareDefault: return 995;
  case Builtins::kSortCompareUserFn: return 996;
  case Builtins::kCanUseSameAccessor25ATGenericElementsAccessor: return 997;
  case Builtins::kCanUseSameAccessor20ATDictionaryElements: return 998;
  case Builtins::kCopyFromTempArray: return 999;
  case Builtins::kCopyWithinSortArray: return 1000;
  case Builtins::kBinaryInsertionSort: return 1001;
  case Builtins::kMergeAt: return 1002;
  case Builtins::kGallopLeft: return 1003;
  case Builtins::kGallopRight: return 1004;
  case Builtins::kArrayTimSort: return 1005;
  case Builtins::kArrayPrototypeSort: return 1006;
  case Builtins::kLoadJoinElement25ATGenericElementsAccessor: return 1007;
  case Builtins::kLoadFixedElement17ATFixedInt32Array: return 1008;
  case Builtins::kStoreFixedElement17ATFixedInt32Array: return 1009;
  case Builtins::kLoadFixedElement19ATFixedFloat32Array: return 1010;
  case Builtins::kStoreFixedElement19ATFixedFloat32Array: return 1011;
  case Builtins::kLoadFixedElement19ATFixedFloat64Array: return 1012;
  case Builtins::kStoreFixedElement19ATFixedFloat64Array: return 1013;
  case Builtins::kLoadFixedElement24ATFixedUint8ClampedArray: return 1014;
  case Builtins::kStoreFixedElement24ATFixedUint8ClampedArray: return 1015;
  case Builtins::kLoadFixedElement21ATFixedBigUint64Array: return 1016;
  case Builtins::kStoreFixedElement21ATFixedBigUint64Array: return 1017;
  case Builtins::kLoadFixedElement20ATFixedBigInt64Array: return 1018;
  case Builtins::kStoreFixedElement20ATFixedBigInt64Array: return 1019;
  case Builtins::kLoadFixedElement17ATFixedUint8Array: return 1020;
  case Builtins::kStoreFixedElement17ATFixedUint8Array: return 1021;
  case Builtins::kLoadFixedElement16ATFixedInt8Array: return 1022;
  case Builtins::kStoreFixedElement16ATFixedInt8Array: return 1023;
  case Builtins::kLoadFixedElement18ATFixedUint16Array: return 1024;
  case Builtins::kStoreFixedElement18ATFixedUint16Array: return 1025;
  case Builtins::kLoadFixedElement17ATFixedInt16Array: return 1026;
  case Builtins::kStoreFixedElement17ATFixedInt16Array: return 1027;
  case Builtins::kLoadFixedElement18ATFixedUint32Array: return 1028;
  case Builtins::kStoreFixedElement18ATFixedUint32Array: return 1029;
  case Builtins::kGenericBuiltinTest5ATSmi: return 1030;
  case Builtins::kLoad25ATGenericElementsAccessor: return 1031;
  case Builtins::kStore25ATGenericElementsAccessor: return 1032;
  case Builtins::kCanUseSameAccessor20ATFastDoubleElements: return 1033;
  case Builtins::kCanUseSameAccessor23ATFastPackedSmiElements: return 1034;
  case Builtins::kCanUseSameAccessor25ATFastSmiOrObjectElements: return 1035;
  case Builtins::kCollatorConstructor: return 1036;
  case Builtins::kCollatorInternalCompare: return 1037;
  case Builtins::kCollatorPrototypeCompare: return 1038;
  case Builtins::kCollatorSupportedLocalesOf: return 1039;
  case Builtins::kCollatorPrototypeResolvedOptions: return 1040;
  case Builtins::kDatePrototypeToLocaleDateString: return 1041;
  case Builtins::kDatePrototypeToLocaleString: return 1042;
  case Builtins::kDatePrototypeToLocaleTimeString: return 1043;
  case Builtins::kDateTimeFormatConstructor: return 1044;
  case Builtins::kDateTimeFormatInternalFormat: return 1045;
  case Builtins::kDateTimeFormatPrototypeFormat: return 1046;
  case Builtins::kDateTimeFormatPrototypeFormatToParts: return 1047;
  case Builtins::kDateTimeFormatPrototypeResolvedOptions: return 1048;
  case Builtins::kDateTimeFormatSupportedLocalesOf: return 1049;
  case Builtins::kIntlGetCanonicalLocales: return 1050;
  case Builtins::kListFormatConstructor: return 1051;
  case Builtins::kListFormatPrototypeFormat: return 1052;
  case Builtins::kListFormatPrototypeFormatToParts: return 1053;
  case Builtins::kListFormatPrototypeResolvedOptions: return 1054;
  case Builtins::kListFormatSupportedLocalesOf: return 1055;
  case Builtins::kLocaleConstructor: return 1056;
  case Builtins::kLocalePrototypeBaseName: return 1057;
  case Builtins::kLocalePrototypeCalendar: return 1058;
  case Builtins::kLocalePrototypeCaseFirst: return 1059;
  case Builtins::kLocalePrototypeCollation: return 1060;
  case Builtins::kLocalePrototypeHourCycle: return 1061;
  case Builtins::kLocalePrototypeLanguage: return 1062;
  case Builtins::kLocalePrototypeMaximize: return 1063;
  case Builtins::kLocalePrototypeMinimize: return 1064;
  case Builtins::kLocalePrototypeNumeric: return 1065;
  case Builtins::kLocalePrototypeNumberingSystem: return 1066;
  case Builtins::kLocalePrototypeRegion: return 1067;
  case Builtins::kLocalePrototypeScript: return 1068;
  case Builtins::kLocalePrototypeToString: return 1069;
  case Builtins::kNumberFormatConstructor: return 1070;
  case Builtins::kNumberFormatInternalFormatNumber: return 1071;
  case Builtins::kNumberFormatPrototypeFormatNumber: return 1072;
  case Builtins::kNumberFormatPrototypeFormatToParts: return 1073;
  case Builtins::kNumberFormatPrototypeResolvedOptions: return 1074;
  case Builtins::kNumberFormatSupportedLocalesOf: return 1075;
  case Builtins::kPluralRulesConstructor: return 1076;
  case Builtins::kPluralRulesPrototypeResolvedOptions: return 1077;
  case Builtins::kPluralRulesPrototypeSelect: return 1078;
  case Builtins::kPluralRulesSupportedLocalesOf: return 1079;
  case Builtins::kRelativeTimeFormatConstructor: return 1080;
  case Builtins::kRelativeTimeFormatPrototypeFormat: return 1081;
  case Builtins::kRelativeTimeFormatPrototypeFormatToParts: return 1082;
  case Builtins::kRelativeTimeFormatPrototypeResolvedOptions: return 1083;
  case Builtins::kRelativeTimeFormatSupportedLocalesOf: return 1084;
  case Builtins::kSegmenterConstructor: return 1085;
  case Builtins::kSegmenterPrototypeResolvedOptions: return 1086;
  case Builtins::kSegmenterPrototypeSegment: return 1087;
  case Builtins::kSegmenterSupportedLocalesOf: return 1088;
  case Builtins::kSegmentIteratorPrototypeBreakType: return 1089;
  case Builtins::kSegmentIteratorPrototypeFollowing: return 1090;
  case Builtins::kSegmentIteratorPrototypePreceding: return 1091;
  case Builtins::kSegmentIteratorPrototypePosition: return 1092;
  case Builtins::kSegmentIteratorPrototypeNext: return 1093;
  case Builtins::kStringPrototypeNormalizeIntl: return 1094;
  case Builtins::kStringPrototypeToLocaleLowerCase: return 1095;
  case Builtins::kStringPrototypeToLocaleUpperCase: return 1096;
  case Builtins::kStringPrototypeToLowerCaseIntl: return 1097;
  case Builtins::kStringPrototypeToUpperCaseIntl: return 1098;
  case Builtins::kStringToLowerCaseIntl: return 1099;
  case Builtins::kV8BreakIteratorConstructor: return 1100;
  case Builtins::kV8BreakIteratorInternalAdoptText: return 1101;
  case Builtins::kV8BreakIteratorInternalBreakType: return 1102;
  case Builtins::kV8BreakIteratorInternalCurrent: return 1103;
  case Builtins::kV8BreakIteratorInternalFirst: return 1104;
  case Builtins::kV8BreakIteratorInternalNext: return 1105;
  case Builtins::kV8BreakIteratorPrototypeAdoptText: return 1106;
  case Builtins::kV8BreakIteratorPrototypeBreakType: return 1107;
  case Builtins::kV8BreakIteratorPrototypeCurrent: return 1108;
  case Builtins::kV8BreakIteratorPrototypeFirst: return 1109;
  case Builtins::kV8BreakIteratorPrototypeNext: return 1110;
  case Builtins::kV8BreakIteratorPrototypeResolvedOptions: return 1111;
  case Builtins::kV8BreakIteratorSupportedLocalesOf: return 1112;
  case Builtins::kWideHandler: return 1113;
  case Builtins::kExtraWideHandler: return 1114;
  case Builtins::kDebugBreakWideHandler: return 1115;
  case Builtins::kDebugBreakExtraWideHandler: return 1116;
  case Builtins::kDebugBreak0Handler: return 1117;
  case Builtins::kDebugBreak1Handler: return 1118;
  case Builtins::kDebugBreak2Handler: return 1119;
  case Builtins::kDebugBreak3Handler: return 1120;
  case Builtins::kDebugBreak4Handler: return 1121;
  case Builtins::kDebugBreak5Handler: return 1122;
  case Builtins::kDebugBreak6Handler: return 1123;
  case Builtins::kLdaLookupContextSlotHandler: return 1124;
  case Builtins::kLdaLookupGlobalSlotHandler: return 1125;
  case Builtins::kLdaLookupSlotInsideTypeofHandler: return 1126;
  case Builtins::kLdaLookupContextSlotInsideTypeofHandler: return 1127;
  case Builtins::kLdaLookupGlobalSlotInsideTypeofHandler: return 1128;
  case Builtins::kLdaModuleVariableHandler: return 1129;
  case Builtins::kStaModuleVariableHandler: return 1130;
  case Builtins::kStaDataPropertyInLiteralHandler: return 1131;
  case Builtins::kCollectTypeProfileHandler: return 1132;
  case Builtins::kModHandler: return 1133;
  case Builtins::kExpHandler: return 1134;
  case Builtins::kShiftRightHandler: return 1135;
  case Builtins::kShiftRightLogicalHandler: return 1136;
  case Builtins::kExpSmiHandler: return 1137;
  case Builtins::kShiftLeftSmiHandler: return 1138;
  case Builtins::kShiftRightSmiHandler: return 1139;
  case Builtins::kShiftRightLogicalSmiHandler: return 1140;
  case Builtins::kGetSuperConstructorHandler: return 1141;
  case Builtins::kCallWithSpreadHandler: return 1142;
  case Builtins::kCallJSRuntimeHandler: return 1143;
  case Builtins::kConstructWithSpreadHandler: return 1144;
  case Builtins::kToNameHandler: return 1145;
  case Builtins::kCreateArrayFromIterableHandler: return 1146;
  case Builtins::kCloneObjectHandler: return 1147;
  case Builtins::kGetTemplateObjectHandler: return 1148;
  case Builtins::kCreateEvalContextHandler: return 1149;
  case Builtins::kCreateRestParameterHandler: return 1150;
  case Builtins::kJumpIfNotNullConstantHandler: return 1151;
  case Builtins::kJumpIfNotUndefinedConstantHandler: return 1152;
  case Builtins::kJumpIfJSReceiverConstantHandler: return 1153;
  case Builtins::kThrowSuperNotCalledIfHoleHandler: return 1154;
  case Builtins::kThrowSuperAlreadyCalledIfNotHoleHandler: return 1155;
  case Builtins::kSwitchOnGeneratorStateHandler: return 1156;
  case Builtins::kSuspendGeneratorHandler: return 1157;
  case Builtins::kResumeGeneratorHandler: return 1158;
  case Builtins::kDebuggerHandler: return 1159;
  case Builtins::kIncBlockCounterHandler: return 1160;
  case Builtins::kAbortHandler: return 1161;
  case Builtins::kIllegalHandler: return 1162;
  case Builtins::kDebugBreak1WideHandler: return 1163;
  case Builtins::kDebugBreak2WideHandler: return 1164;
  case Builtins::kDebugBreak3WideHandler: return 1165;
  case Builtins::kDebugBreak4WideHandler: return 1166;
  case Builtins::kDebugBreak5WideHandler: return 1167;
  case Builtins::kDebugBreak6WideHandler: return 1168;
  case Builtins::kLdaSmiWideHandler: return 1169;
  case Builtins::kLdaConstantWideHandler: return 1170;
  case Builtins::kLdaGlobalWideHandler: return 1171;
  case Builtins::kLdaGlobalInsideTypeofWideHandler: return 1172;
  case Builtins::kStaGlobalWideHandler: return 1173;
  case Builtins::kPushContextWideHandler: return 1174;
  case Builtins::kPopContextWideHandler: return 1175;
  case Builtins::kLdaContextSlotWideHandler: return 1176;
  case Builtins::kLdaImmutableContextSlotWideHandler: return 1177;
  case Builtins::kLdaCurrentContextSlotWideHandler: return 1178;
  case Builtins::kLdaImmutableCurrentContextSlotWideHandler: return 1179;
  case Builtins::kStaContextSlotWideHandler: return 1180;
  case Builtins::kStaCurrentContextSlotWideHandler: return 1181;
  case Builtins::kLdaLookupSlotWideHandler: return 1182;
  case Builtins::kLdaLookupContextSlotWideHandler: return 1183;
  case Builtins::kLdaLookupGlobalSlotWideHandler: return 1184;
  case Builtins::kLdaLookupSlotInsideTypeofWideHandler: return 1185;
  case Builtins::kLdaLookupContextSlotInsideTypeofWideHandler: return 1186;
  case Builtins::kLdaLookupGlobalSlotInsideTypeofWideHandler: return 1187;
  case Builtins::kStaLookupSlotWideHandler: return 1188;
  case Builtins::kLdarWideHandler: return 1189;
  case Builtins::kStarWideHandler: return 1190;
  case Builtins::kMovWideHandler: return 1191;
  case Builtins::kLdaNamedPropertyWideHandler: return 1192;
  case Builtins::kLdaNamedPropertyNoFeedbackWideHandler: return 1193;
  case Builtins::kLdaKeyedPropertyWideHandler: return 1194;
  case Builtins::kLdaModuleVariableWideHandler: return 1195;
  case Builtins::kStaModuleVariableWideHandler: return 1196;
  case Builtins::kStaNamedPropertyWideHandler: return 1197;
  case Builtins::kStaNamedPropertyNoFeedbackWideHandler: return 1198;
  case Builtins::kStaNamedOwnPropertyWideHandler: return 1199;
  case Builtins::kStaKeyedPropertyWideHandler: return 1200;
  case Builtins::kStaInArrayLiteralWideHandler: return 1201;
  case Builtins::kStaDataPropertyInLiteralWideHandler: return 1202;
  case Builtins::kCollectTypeProfileWideHandler: return 1203;
  case Builtins::kAddWideHandler: return 1204;
  case Builtins::kSubWideHandler: return 1205;
  case Builtins::kMulWideHandler: return 1206;
  case Builtins::kDivWideHandler: return 1207;
  case Builtins::kModWideHandler: return 1208;
  case Builtins::kExpWideHandler: return 1209;
  case Builtins::kBitwiseOrWideHandler: return 1210;
  case Builtins::kBitwiseXorWideHandler: return 1211;
  case Builtins::kBitwiseAndWideHandler: return 1212;
  case Builtins::kShiftLeftWideHandler: return 1213;
  case Builtins::kShiftRightWideHandler: return 1214;
  case Builtins::kShiftRightLogicalWideHandler: return 1215;
  case Builtins::kAddSmiWideHandler: return 1216;
  case Builtins::kSubSmiWideHandler: return 1217;
  case Builtins::kMulSmiWideHandler: return 1218;
  case Builtins::kDivSmiWideHandler: return 1219;
  case Builtins::kModSmiWideHandler: return 1220;
  case Builtins::kExpSmiWideHandler: return 1221;
  case Builtins::kBitwiseOrSmiWideHandler: return 1222;
  case Builtins::kBitwiseXorSmiWideHandler: return 1223;
  case Builtins::kBitwiseAndSmiWideHandler: return 1224;
  case Builtins::kShiftLeftSmiWideHandler: return 1225;
  case Builtins::kShiftRightSmiWideHandler: return 1226;
  case Builtins::kShiftRightLogicalSmiWideHandler: return 1227;
  case Builtins::kIncWideHandler: return 1228;
  case Builtins::kDecWideHandler: return 1229;
  case Builtins::kNegateWideHandler: return 1230;
  case Builtins::kBitwiseNotWideHandler: return 1231;
  case Builtins::kDeletePropertyStrictWideHandler: return 1232;
  case Builtins::kDeletePropertySloppyWideHandler: return 1233;
  case Builtins::kGetSuperConstructorWideHandler: return 1234;
  case Builtins::kCallAnyReceiverWideHandler: return 1235;
  case Builtins::kCallPropertyWideHandler: return 1236;
  case Builtins::kCallProperty0WideHandler: return 1237;
  case Builtins::kCallProperty1WideHandler: return 1238;
  case Builtins::kCallProperty2WideHandler: return 1239;
  case Builtins::kCallUndefinedReceiverWideHandler: return 1240;
  case Builtins::kCallUndefinedReceiver0WideHandler: return 1241;
  case Builtins::kCallUndefinedReceiver1WideHandler: return 1242;
  case Builtins::kCallUndefinedReceiver2WideHandler: return 1243;
  case Builtins::kCallNoFeedbackWideHandler: return 1244;
  case Builtins::kCallWithSpreadWideHandler: return 1245;
  case Builtins::kCallRuntimeWideHandler: return 1246;
  case Builtins::kCallRuntimeForPairWideHandler: return 1247;
  case Builtins::kCallJSRuntimeWideHandler: return 1248;
  case Builtins::kInvokeIntrinsicWideHandler: return 1249;
  case Builtins::kConstructWideHandler: return 1250;
  case Builtins::kConstructWithSpreadWideHandler: return 1251;
  case Builtins::kTestEqualWideHandler: return 1252;
  case Builtins::kTestEqualStrictWideHandler: return 1253;
  case Builtins::kTestLessThanWideHandler: return 1254;
  case Builtins::kTestGreaterThanWideHandler: return 1255;
  case Builtins::kTestLessThanOrEqualWideHandler: return 1256;
  case Builtins::kTestGreaterThanOrEqualWideHandler: return 1257;
  case Builtins::kTestReferenceEqualWideHandler: return 1258;
  case Builtins::kTestInstanceOfWideHandler: return 1259;
  case Builtins::kTestInWideHandler: return 1260;
  case Builtins::kToNameWideHandler: return 1261;
  case Builtins::kToNumberWideHandler: return 1262;
  case Builtins::kToNumericWideHandler: return 1263;
  case Builtins::kToObjectWideHandler: return 1264;
  case Builtins::kCreateRegExpLiteralWideHandler: return 1265;
  case Builtins::kCreateArrayLiteralWideHandler: return 1266;
  case Builtins::kCreateEmptyArrayLiteralWideHandler: return 1267;
  case Builtins::kCreateObjectLiteralWideHandler: return 1268;
  case Builtins::kCloneObjectWideHandler: return 1269;
  case Builtins::kGetTemplateObjectWideHandler: return 1270;
  case Builtins::kCreateClosureWideHandler: return 1271;
  case Builtins::kCreateBlockContextWideHandler: return 1272;
  case Builtins::kCreateCatchContextWideHandler: return 1273;
  case Builtins::kCreateFunctionContextWideHandler: return 1274;
  case Builtins::kCreateEvalContextWideHandler: return 1275;
  case Builtins::kCreateWithContextWideHandler: return 1276;
  case Builtins::kJumpLoopWideHandler: return 1277;
  case Builtins::kJumpWideHandler: return 1278;
  case Builtins::kJumpConstantWideHandler: return 1279;
  case Builtins::kJumpIfNullConstantWideHandler: return 1280;
  case Builtins::kJumpIfNotNullConstantWideHandler: return 1281;
  case Builtins::kJumpIfUndefinedConstantWideHandler: return 1282;
  case Builtins::kJumpIfNotUndefinedConstantWideHandler: return 1283;
  case Builtins::kJumpIfTrueConstantWideHandler: return 1284;
  case Builtins::kJumpIfFalseConstantWideHandler: return 1285;
  case Builtins::kJumpIfJSReceiverConstantWideHandler: return 1286;
  case Builtins::kJumpIfToBooleanTrueConstantWideHandler: return 1287;
  case Builtins::kJumpIfToBooleanFalseConstantWideHandler: return 1288;
  case Builtins::kJumpIfToBooleanTrueWideHandler: return 1289;
  case Builtins::kJumpIfToBooleanFalseWideHandler: return 1290;
  case Builtins::kJumpIfTrueWideHandler: return 1291;
  case Builtins::kJumpIfFalseWideHandler: return 1292;
  case Builtins::kJumpIfNullWideHandler: return 1293;
  case Builtins::kJumpIfNotNullWideHandler: return 1294;
  case Builtins::kJumpIfUndefinedWideHandler: return 1295;
  case Builtins::kJumpIfNotUndefinedWideHandler: return 1296;
  case Builtins::kJumpIfJSReceiverWideHandler: return 1297;
  case Builtins::kSwitchOnSmiNoFeedbackWideHandler: return 1298;
  case Builtins::kForInEnumerateWideHandler: return 1299;
  case Builtins::kForInPrepareWideHandler: return 1300;
  case Builtins::kForInContinueWideHandler: return 1301;
  case Builtins::kForInNextWideHandler: return 1302;
  case Builtins::kForInStepWideHandler: return 1303;
  case Builtins::kThrowReferenceErrorIfHoleWideHandler: return 1304;
  case Builtins::kSwitchOnGeneratorStateWideHandler: return 1305;
  case Builtins::kSuspendGeneratorWideHandler: return 1306;
  case Builtins::kResumeGeneratorWideHandler: return 1307;
  case Builtins::kIncBlockCounterWideHandler: return 1308;
  case Builtins::kAbortWideHandler: return 1309;
  case Builtins::kDebugBreak1ExtraWideHandler: return 1310;
  case Builtins::kDebugBreak2ExtraWideHandler: return 1311;
  case Builtins::kDebugBreak3ExtraWideHandler: return 1312;
  case Builtins::kDebugBreak4ExtraWideHandler: return 1313;
  case Builtins::kDebugBreak5ExtraWideHandler: return 1314;
  case Builtins::kDebugBreak6ExtraWideHandler: return 1315;
  case Builtins::kLdaSmiExtraWideHandler: return 1316;
  case Builtins::kLdaConstantExtraWideHandler: return 1317;
  case Builtins::kLdaGlobalExtraWideHandler: return 1318;
  case Builtins::kLdaGlobalInsideTypeofExtraWideHandler: return 1319;
  case Builtins::kStaGlobalExtraWideHandler: return 1320;
  case Builtins::kPushContextExtraWideHandler: return 1321;
  case Builtins::kPopContextExtraWideHandler: return 1322;
  case Builtins::kLdaContextSlotExtraWideHandler: return 1323;
  case Builtins::kLdaImmutableContextSlotExtraWideHandler: return 1324;
  case Builtins::kLdaCurrentContextSlotExtraWideHandler: return 1325;
  case Builtins::kLdaImmutableCurrentContextSlotExtraWideHandler: return 1326;
  case Builtins::kStaContextSlotExtraWideHandler: return 1327;
  case Builtins::kStaCurrentContextSlotExtraWideHandler: return 1328;
  case Builtins::kLdaLookupSlotExtraWideHandler: return 1329;
  case Builtins::kLdaLookupContextSlotExtraWideHandler: return 1330;
  case Builtins::kLdaLookupGlobalSlotExtraWideHandler: return 1331;
  case Builtins::kLdaLookupSlotInsideTypeofExtraWideHandler: return 1332;
  case Builtins::kLdaLookupContextSlotInsideTypeofExtraWideHandler: return 1333;
  case Builtins::kLdaLookupGlobalSlotInsideTypeofExtraWideHandler: return 1334;
  case Builtins::kStaLookupSlotExtraWideHandler: return 1335;
  case Builtins::kLdarExtraWideHandler: return 1336;
  case Builtins::kStarExtraWideHandler: return 1337;
  case Builtins::kMovExtraWideHandler: return 1338;
  case Builtins::kLdaNamedPropertyExtraWideHandler: return 1339;
  case Builtins::kLdaNamedPropertyNoFeedbackExtraWideHandler: return 1340;
  case Builtins::kLdaKeyedPropertyExtraWideHandler: return 1341;
  case Builtins::kLdaModuleVariableExtraWideHandler: return 1342;
  case Builtins::kStaModuleVariableExtraWideHandler: return 1343;
  case Builtins::kStaNamedPropertyExtraWideHandler: return 1344;
  case Builtins::kStaNamedPropertyNoFeedbackExtraWideHandler: return 1345;
  case Builtins::kStaNamedOwnPropertyExtraWideHandler: return 1346;
  case Builtins::kStaKeyedPropertyExtraWideHandler: return 1347;
  case Builtins::kStaInArrayLiteralExtraWideHandler: return 1348;
  case Builtins::kStaDataPropertyInLiteralExtraWideHandler: return 1349;
  case Builtins::kCollectTypeProfileExtraWideHandler: return 1350;
  case Builtins::kAddExtraWideHandler: return 1351;
  case Builtins::kSubExtraWideHandler: return 1352;
  case Builtins::kMulExtraWideHandler: return 1353;
  case Builtins::kDivExtraWideHandler: return 1354;
  case Builtins::kModExtraWideHandler: return 1355;
  case Builtins::kExpExtraWideHandler: return 1356;
  case Builtins::kBitwiseOrExtraWideHandler: return 1357;
  case Builtins::kBitwiseXorExtraWideHandler: return 1358;
  case Builtins::kBitwiseAndExtraWideHandler: return 1359;
  case Builtins::kShiftLeftExtraWideHandler: return 1360;
  case Builtins::kShiftRightExtraWideHandler: return 1361;
  case Builtins::kShiftRightLogicalExtraWideHandler: return 1362;
  case Builtins::kAddSmiExtraWideHandler: return 1363;
  case Builtins::kSubSmiExtraWideHandler: return 1364;
  case Builtins::kMulSmiExtraWideHandler: return 1365;
  case Builtins::kDivSmiExtraWideHandler: return 1366;
  case Builtins::kModSmiExtraWideHandler: return 1367;
  case Builtins::kExpSmiExtraWideHandler: return 1368;
  case Builtins::kBitwiseOrSmiExtraWideHandler: return 1369;
  case Builtins::kBitwiseXorSmiExtraWideHandler: return 1370;
  case Builtins::kBitwiseAndSmiExtraWideHandler: return 1371;
  case Builtins::kShiftLeftSmiExtraWideHandler: return 1372;
  case Builtins::kShiftRightSmiExtraWideHandler: return 1373;
  case Builtins::kShiftRightLogicalSmiExtraWideHandler: return 1374;
  case Builtins::kIncExtraWideHandler: return 1375;
  case Builtins::kDecExtraWideHandler: return 1376;
  case Builtins::kNegateExtraWideHandler: return 1377;
  case Builtins::kBitwiseNotExtraWideHandler: return 1378;
  case Builtins::kDeletePropertyStrictExtraWideHandler: return 1379;
  case Builtins::kDeletePropertySloppyExtraWideHandler: return 1380;
  case Builtins::kGetSuperConstructorExtraWideHandler: return 1381;
  case Builtins::kCallAnyReceiverExtraWideHandler: return 1382;
  case Builtins::kCallPropertyExtraWideHandler: return 1383;
  case Builtins::kCallProperty0ExtraWideHandler: return 1384;
  case Builtins::kCallProperty1ExtraWideHandler: return 1385;
  case Builtins::kCallProperty2ExtraWideHandler: return 1386;
  case Builtins::kCallUndefinedReceiverExtraWideHandler: return 1387;
  case Builtins::kCallUndefinedReceiver0ExtraWideHandler: return 1388;
  case Builtins::kCallUndefinedReceiver1ExtraWideHandler: return 1389;
  case Builtins::kCallUndefinedReceiver2ExtraWideHandler: return 1390;
  case Builtins::kCallNoFeedbackExtraWideHandler: return 1391;
  case Builtins::kCallWithSpreadExtraWideHandler: return 1392;
  case Builtins::kCallRuntimeExtraWideHandler: return 1393;
  case Builtins::kCallRuntimeForPairExtraWideHandler: return 1394;
  case Builtins::kCallJSRuntimeExtraWideHandler: return 1395;
  case Builtins::kInvokeIntrinsicExtraWideHandler: return 1396;
  case Builtins::kConstructExtraWideHandler: return 1397;
  case Builtins::kConstructWithSpreadExtraWideHandler: return 1398;
  case Builtins::kTestEqualExtraWideHandler: return 1399;
  case Builtins::kTestEqualStrictExtraWideHandler: return 1400;
  case Builtins::kTestLessThanExtraWideHandler: return 1401;
  case Builtins::kTestGreaterThanExtraWideHandler: return 1402;
  case Builtins::kTestLessThanOrEqualExtraWideHandler: return 1403;
  case Builtins::kTestGreaterThanOrEqualExtraWideHandler: return 1404;
  case Builtins::kTestReferenceEqualExtraWideHandler: return 1405;
  case Builtins::kTestInstanceOfExtraWideHandler: return 1406;
  case Builtins::kTestInExtraWideHandler: return 1407;
  case Builtins::kToNameExtraWideHandler: return 1408;
  case Builtins::kToNumberExtraWideHandler: return 1409;
  case Builtins::kToNumericExtraWideHandler: return 1410;
  case Builtins::kToObjectExtraWideHandler: return 1411;
  case Builtins::kCreateRegExpLiteralExtraWideHandler: return 1412;
  case Builtins::kCreateArrayLiteralExtraWideHandler: return 1413;
  case Builtins::kCreateEmptyArrayLiteralExtraWideHandler: return 1414;
  case Builtins::kCreateObjectLiteralExtraWideHandler: return 1415;
  case Builtins::kCloneObjectExtraWideHandler: return 1416;
  case Builtins::kGetTemplateObjectExtraWideHandler: return 1417;
  case Builtins::kCreateClosureExtraWideHandler: return 1418;
  case Builtins::kCreateBlockContextExtraWideHandler: return 1419;
  case Builtins::kCreateCatchContextExtraWideHandler: return 1420;
  case Builtins::kCreateFunctionContextExtraWideHandler: return 1421;
  case Builtins::kCreateEvalContextExtraWideHandler: return 1422;
  case Builtins::kCreateWithContextExtraWideHandler: return 1423;
  case Builtins::kJumpLoopExtraWideHandler: return 1424;
  case Builtins::kJumpExtraWideHandler: return 1425;
  case Builtins::kJumpConstantExtraWideHandler: return 1426;
  case Builtins::kJumpIfNullConstantExtraWideHandler: return 1427;
  case Builtins::kJumpIfNotNullConstantExtraWideHandler: return 1428;
  case Builtins::kJumpIfUndefinedConstantExtraWideHandler: return 1429;
  case Builtins::kJumpIfNotUndefinedConstantExtraWideHandler: return 1430;
  case Builtins::kJumpIfTrueConstantExtraWideHandler: return 1431;
  case Builtins::kJumpIfFalseConstantExtraWideHandler: return 1432;
  case Builtins::kJumpIfJSReceiverConstantExtraWideHandler: return 1433;
  case Builtins::kJumpIfToBooleanTrueConstantExtraWideHandler: return 1434;
  case Builtins::kJumpIfToBooleanFalseConstantExtraWideHandler: return 1435;
  case Builtins::kJumpIfToBooleanTrueExtraWideHandler: return 1436;
  case Builtins::kJumpIfToBooleanFalseExtraWideHandler: return 1437;
  case Builtins::kJumpIfTrueExtraWideHandler: return 1438;
  case Builtins::kJumpIfFalseExtraWideHandler: return 1439;
  case Builtins::kJumpIfNullExtraWideHandler: return 1440;
  case Builtins::kJumpIfNotNullExtraWideHandler: return 1441;
  case Builtins::kJumpIfUndefinedExtraWideHandler: return 1442;
  case Builtins::kJumpIfNotUndefinedExtraWideHandler: return 1443;
  case Builtins::kJumpIfJSReceiverExtraWideHandler: return 1444;
  case Builtins::kSwitchOnSmiNoFeedbackExtraWideHandler: return 1445;
  case Builtins::kForInEnumerateExtraWideHandler: return 1446;
  case Builtins::kForInPrepareExtraWideHandler: return 1447;
  case Builtins::kForInContinueExtraWideHandler: return 1448;
  case Builtins::kForInNextExtraWideHandler: return 1449;
  case Builtins::kForInStepExtraWideHandler: return 1450;
  case Builtins::kThrowReferenceErrorIfHoleExtraWideHandler: return 1451;
  case Builtins::kSwitchOnGeneratorStateExtraWideHandler: return 1452;
  case Builtins::kSuspendGeneratorExtraWideHandler: return 1453;
  case Builtins::kResumeGeneratorExtraWideHandler: return 1454;
  case Builtins::kIncBlockCounterExtraWideHandler: return 1455;
  case Builtins::kAbortExtraWideHandler: return 1456;
  }
  STATIC_ASSERT(EmbeddedData::kBuiltinCount == 1457);
  UNREACHABLE();
}

}  // namespace internal
}  // namespace v8

#endif  // V8_SNAPSHOT_EMBEDDED_DATA_H_
