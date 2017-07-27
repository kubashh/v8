// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-proxy-helpers-gen.h"

namespace v8 {
namespace internal {
Node* ProxyHelpersCodeStubAssembler::ProxyGetProperty(Node* context,
                                                      Node* proxy, Node* name,
                                                      Node* receiver) {
  Print("ProxyGetProperty", name);
  VARIABLE(result, MachineRepresentation::kTagged, SmiConstant(0));
  VARIABLE(traverse, MachineRepresentation::kTagged, TrueConstant());
  VARIABLE(target, MachineRepresentation::kTagged, proxy);
  Variable* vars[] = {&result, &target, &traverse};
  Label done(this), loop(this, 3, vars);

  // 1. Assert: IsPropertyKey(P) is true.
  CSA_ASSERT(this, IsPropertyKey(name));
  Goto(&loop);
  BIND(&loop);
  {
    GotoIfNot(WordEqual(traverse.value(), TrueConstant()), &done);
    result.Bind(ProxyGetPropertyWithResult(context, target.value(), name,
                                           receiver, &target, &traverse));
    Goto(&loop);
  }

  BIND(&done);
  return result.value();
}

Node* ProxyHelpersCodeStubAssembler::ProxyGetPropertyWithResult(
    Node* context, Node* proxy, Node* name, Node* receiver, Variable* target,
    Variable* traverse) {
  Print("ProxyGetPropertyWithResult with proxy", proxy);
  CSA_ASSERT(this, IsJSProxy(proxy));

  Label throw_proxy_handler_revoked(this, Label::kDeferred),
      trap_undefined(this);
  // 2. Let handler be O.[[ProxyHandler]].
  Node* handler = LoadObjectField(proxy, JSProxy::kHandlerOffset);

  // 3. If handler is null, throw a TypeError exception.
  CSA_ASSERT(this, IsNullOrJSReceiver(handler));
  GotoIf(IsNull(handler), &throw_proxy_handler_revoked);

  // 4. Assert: Type(handler) is Object.
  CSA_ASSERT(this, IsJSReceiver(handler));

  // 5. Let target be O.[[ProxyTarget]].
  // Node* target = LoadObjectField(proxy, JSProxy::kTargetOffset);
  target->Bind(LoadObjectField(proxy, JSProxy::kTargetOffset));

  // 6. Let trap be ? GetMethod(handler, "get").
  // 7. If trap is undefined, then
  Handle<Name> trap_name = factory()->get_string();
  Node* trap = GetMethod(context, handler, trap_name, &trap_undefined);

  // 8. Let trapResult be ? Call(trap, handler, « target, P, Receiver »).
  Node* trap_result = CallJS(CodeFactory::Call(isolate()), context, trap,
                             handler, target->value(), name, receiver);
  // 9. Let targetDesc be ? target.[[GetOwnProperty]](P).
  Label slow_call_runtime(this, Label::kDeferred), done(this),
      if_found_value(this, Label::kDeferred), if_not_found(this);
  CheckTrapResult(context, target->value(), proxy, name, trap_result,
                  &if_found_value, &if_not_found, &slow_call_runtime);

  // We've reached the last proxy in the prototype chain
  VARIABLE(result, MachineRepresentation::kTagged);

  BIND(&slow_call_runtime);
  {
    Print("Slow");
    result.Bind(SmiConstant(0));
    Goto(&done);
  }

  BIND(&if_not_found);
  {
    // 11. Return trapResult.
    Print("Returning result");
    traverse->Bind(FalseConstant());
    result.Bind(trap_result);
    Goto(&done);
  }

  BIND(&trap_undefined);
  {
    // 7.a. Return ? target.[[Get]](P, Receiver).
    // TODO(mslekova) : where do we pass Receiver?
    Print("Trap undefined");
    traverse->Bind(FalseConstant());
    result.Bind(GetProperty(context, target->value(), name));
    Goto(&done);
  }

  BIND(&throw_proxy_handler_revoked);
  { ThrowTypeError(context, MessageTemplate::kProxyRevoked, "get"); }

  BIND(&done);
  return result.value();
}

void ProxyHelpersCodeStubAssembler::CheckTrapResult(
    Node* context, Node* target, Node* proxy, Node* name, Node* trap_result,
    Label* if_found_value, Label* if_not_found, Label* slow_call_runtime) {
  Node* map = LoadMap(target);
  VARIABLE(var_value, MachineRepresentation::kTagged);
  VARIABLE(var_details, MachineRepresentation::kWord32);
  VARIABLE(var_raw_value, MachineRepresentation::kTagged);

  Node* instance_type = LoadInstanceType(target);
  TryGetOwnProperty(context, proxy, target, map, instance_type, name,
                    if_found_value, &var_value, &var_details, &var_raw_value,
                    if_not_found, slow_call_runtime);

  BIND(if_found_value);
  {
    Label target_desc_writable(this, Label::kDeferred),
        target_desc_no_get(this, Label::kDeferred),
        trap_result_different(this, Label::kDeferred),
        throw_non_configurable_data(this, Label::kDeferred),
        throw_non_configurable_accessor(this, Label::kDeferred),
        check_accessor(this), check_data(this);

    // 10. If targetDesc is not undefined and targetDesc.[[Configurable]] is
    // false, then
    Print("Checking for IsConfigurable, target", target);
    GotoIfNot(IsSetWord32(var_details.value(),
                          PropertyDetails::kAttributesDontDeleteMask),
              if_not_found);

    // 10.a. If IsDataDescriptor(targetDesc) is true and
    // targetDesc.[[Writable]] is false, then
    Print("Checking for BranchIfAccessorPair");
    BranchIfAccessorPair(var_raw_value.value(), &check_accessor, &check_data);

    BIND(&check_data);
    {
      Print("Checking writable");
      Node* read_only = IsSetWord32(var_details.value(),
                                    PropertyDetails::kAttributesReadOnlyMask);
      GotoIfNot(read_only, &check_accessor);

      // 10.a.i. If SameValue(trapResult, targetDesc.[[Value]]) is false,
      // throw a TypeError exception.
      Print("Checking SameValue");
      GotoIfNot(SameValue(trap_result, var_value.value()),
                &throw_non_configurable_data);
      Goto(&check_accessor);
    }

    BIND(&check_accessor);
    {
      // 10.b. If IsAccessorDescriptor(targetDesc) is true and
      // targetDesc.[[Get]] is undefined, then
      Print("Checking get");
      Node* accessor_pair = var_raw_value.value();
      Node* getter =
          LoadObjectField(accessor_pair, AccessorPair::kGetterOffset);

      Print("Getter", getter);
      // Here we check for null as well because if the getter was never
      // defined it's set as null
      GotoIfNot(Word32Or(IsUndefined(getter), IsNull(getter)), if_not_found);

      // 10.b.i. If trapResult is not undefined, throw a TypeError exception.
      Print("Checking trap_result", trap_result);
      GotoIfNot(IsUndefined(trap_result), &throw_non_configurable_accessor);
      Goto(if_not_found);
    }

    BIND(&throw_non_configurable_data);
    {
      ThrowTypeError(context, MessageTemplate::kProxyGetNonConfigurableData,
                     name, var_value.value(), trap_result);
    }

    BIND(&throw_non_configurable_accessor);
    {
      ThrowTypeError(context, MessageTemplate::kProxyGetNonConfigurableAccessor,
                     name, trap_result);
    }
  }
}
}  // namespace internal
}  // namespace v8
