// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-utils-gen.h"
#include "src/builtins/builtins.h"
#include "src/code-stub-assembler.h"
#include "src/objects/api-callbacks.h"
#include "src/objects/templates.h"
#include "src/runtime/runtime.h"

namespace v8 {
namespace internal {

TF_BUILTIN(CallApiCallbackWithChecks, CodeStubAssembler) {
  TNode<Context> context = CAST(Parameter(Descriptor::kContext));
  TNode<FunctionTemplateInfo> function_template_info =
      CAST(Parameter(Descriptor::kFunctionTemplateInfo));
  TNode<IntPtrT> argc =
      UncheckedCast<IntPtrT>(Parameter(Descriptor::kActualArgumentsCount));
  CodeStubArguments args(this, argc);

  // For API callbacks we need to call ToObject on the receiver.
  // And in case the receiver is a JSObject already, we might
  // need to perform access checks in the current {context},
  // dependending on whether the "needs access check" bit is
  // set on the receiver _and_ the {function_template_info}
  // doesn't have the "accepts any receiver" bit set.
  TVARIABLE(Object, var_receiver, args.GetReceiver());
  {
    Label receiver_is_primitive(this, Label::kDeferred),
        receiver_needs_access_check(this, Label::kDeferred),
        receiver_done(this);

    // Check if the receiver needs to be converted, or if it's already
    // a JSReceiver, see if the "needs access check" bit is set _and_
    // the {function_template_info} doesn't just accept any receiver.
    GotoIf(TaggedIsSmi(var_receiver.value()), &receiver_is_primitive);
    TNode<Map> receiver_map = LoadMap(CAST(var_receiver.value()));
    GotoIfNot(IsJSReceiverMap(receiver_map), &receiver_is_primitive);
    GotoIfNot(
        IsSetWord32<Map::IsAccessCheckNeededBit>(LoadMapBitField(receiver_map)),
        &receiver_done);
    TNode<WordT> function_template_info_flags = LoadAndUntagObjectField(
        function_template_info, FunctionTemplateInfo::kFlagOffset);
    Branch(IsSetWord(function_template_info_flags,
                     1 << FunctionTemplateInfo::kAcceptAnyReceiver),
           &receiver_done, &receiver_needs_access_check);

    BIND(&receiver_is_primitive);
    {
      // Convert primitives to wrapper objects as necessary. These
      // wrappers will never have the "access check needed" bit set,
      // so we don't need to loop into the above checking again.
      var_receiver = ToObject(context, var_receiver.value());
      CSA_ASSERT(this,
                 IsClearWord32<Map::IsAccessCheckNeededBit>(
                     LoadMapBitField(LoadMap(CAST(var_receiver.value())))));
      args.SetReceiver(var_receiver.value());
      Goto(&receiver_done);
    }

    BIND(&receiver_needs_access_check);
    {
      // Ask the runtime to perform the necessary access check for
      // the receiver in the current {context}.
      CallRuntime(Runtime::kAccessCheck, context, var_receiver.value());
      Goto(&receiver_done);
    }

    BIND(&receiver_done);
  }

  // If the {function_template_info} doesn't specify any signature, we
  // just use the receiver as the holder for the API callback, otherwise
  // we need to check that receiver (or it's hidden prototype) are
  // compatible for the signature.
  TNode<HeapObject> signature = LoadObjectField<HeapObject>(
      function_template_info, FunctionTemplateInfo::kSignatureOffset);
  TVARIABLE(HeapObject, var_holder, CAST(var_receiver.value()));
  Label holder_found(this, &var_holder);
  GotoIf(IsUndefined(signature), &holder_found);
  {
    // Walk up the hidden prototype chain to find the compatible holder
    // for the {signature}, starting with the receiver itself.
    //
    // Be careful, these loops are hand-tuned for (close to) ideal CSA
    // code generation. Especially the sharing of the {var_template}
    // below is intentional (even though it reads a bit funny in the
    // first loop).
    Label holder_loop(this, &var_holder), holder_next(this, Label::kDeferred);
    Goto(&holder_loop);
    BIND(&holder_loop);
    {
      // Find the template to compare against the {signature}. We don't
      // bother checking that the template is a FunctionTemplateInfo here,
      // but instead do that as part of the template loop below. The only
      // thing we care about is that the template is actually a HeapObject.
      TNode<HeapObject> holder = var_holder.value();
      TVARIABLE(HeapObject, var_template, LoadMap(holder));
      Label template_map_loop(this, &var_template),
          template_loop(this, &var_template),
          template_from_closure(this, &var_template);
      Goto(&template_map_loop);
      BIND(&template_map_loop);
      {
        // Load the constructor field from the current map (in the
        // {var_template} variable), and see if that is a HeapObject.
        // If it's a Smi then it is non-instance prototype on some
        // initial map, which cannot be the case for API instances.
        TNode<Object> constructor = LoadObjectField(
            var_template.value(), Map::kConstructorOrBackPointerOffset);
        GotoIf(TaggedIsSmi(constructor), &holder_next);

        // Now there are three cases for {constructor} that we care
        // about here:
        //
        //  1. {constructor} is a JSFunction, and we can load the template
        //     from it's SharedFunctionInfo::function_data field (which
        //     may not actually be a FunctionTemplateInfo).
        //  2. {constructor} is a Map, in which case it's not a constructor
        //     but a back-pointer and we follow that.
        //  3. {constructor} is a FunctionTemplateInfo (or some other
        //     HeapObject), in which case we can directly use that for
        //     the template loop below (non-FunctionTemplateInfo objects
        //     will be ruled out there).
        //
        var_template = CAST(constructor);
        TNode<Int32T> template_type = LoadInstanceType(var_template.value());
        GotoIf(InstanceTypeEqual(template_type, JS_FUNCTION_TYPE),
               &template_from_closure);
        Branch(InstanceTypeEqual(template_type, MAP_TYPE), &template_map_loop,
               &template_loop);
      }

      BIND(&template_from_closure);
      {
        // The first case from above, where we load the template from the
        // SharedFunctionInfo of the closure. We only check that the
        // SharedFunctionInfo::function_data is a HeapObject and blindly
        // use that as a template, since a non-FunctionTemplateInfo objects
        // will be ruled out automatically by the template loop below.
        TNode<SharedFunctionInfo> template_shared =
            LoadObjectField<SharedFunctionInfo>(
                var_template.value(), JSFunction::kSharedFunctionInfoOffset);
        TNode<Object> template_data = LoadObjectField(
            template_shared, SharedFunctionInfo::kFunctionDataOffset);
        GotoIf(TaggedIsSmi(template_data), &holder_next);
        var_template = CAST(template_data);
        Goto(&template_loop);
      }

      BIND(&template_loop);
      {
        // This loop compares the template to the expected {signature},
        // following the chain of parent templates until it hits the
        // end, in which case we continue with the next holder (the
        // hidden prototype) if there's any.
        TNode<HeapObject> current = var_template.value();
        GotoIf(WordEqual(current, signature), &holder_found);

        GotoIfNot(IsFunctionTemplateInfoMap(LoadMap(current)), &holder_next);

        TNode<HeapObject> current_rare = LoadObjectField<HeapObject>(
            current, FunctionTemplateInfo::kFunctionTemplateRareDataOffset);
        GotoIf(IsUndefined(current_rare), &holder_next);
        var_template = LoadObjectField<HeapObject>(
            current_rare, FunctionTemplateRareData::kParentTemplateOffset);
        Goto(&template_loop);
      }

      BIND(&holder_next);
      {
        // Continue with the hidden prototype of the {holder} if it
        // has one, or throw an illegal invocation exception, since
        // the receiver did not pass the {signature} check.
        TNode<Map> holder_map = LoadMap(holder);
        var_holder = LoadMapPrototype(holder_map);
        GotoIf(IsSetWord32(LoadMapBitField3(holder_map),
                           Map::HasHiddenPrototypeBit::kMask),
               &holder_loop);
        ThrowTypeError(context, MessageTemplate::kIllegalInvocation);
      }
    }
  }
  BIND(&holder_found);
  TNode<JSReceiver> holder = CAST(var_holder.value());

  // Perform the actual API callback invocation via the CallApiCallback
  // builtin.
  TNode<CallHandlerInfo> call_handler_info = LoadObjectField<CallHandlerInfo>(
      function_template_info, FunctionTemplateInfo::kCallCodeOffset);
  TNode<Foreign> foreign = LoadObjectField<Foreign>(
      call_handler_info, CallHandlerInfo::kJsCallbackOffset);
  TNode<RawPtrT> callback =
      LoadObjectField<RawPtrT>(foreign, Foreign::kForeignAddressOffset);
  TNode<Object> call_data =
      LoadObjectField<Object>(call_handler_info, CallHandlerInfo::kDataOffset);
  TailCallStub(CodeFactory::CallApiCallback(isolate()), context, callback, argc,
               call_data, holder);
}

}  // namespace internal
}  // namespace v8
