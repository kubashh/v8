// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_IC_CALL_OPTIMIZATION_H_
#define V8_IC_CALL_OPTIMIZATION_H_

#include "src/api-arguments.h"
#include "src/objects.h"

namespace v8 {
namespace internal {
// Holds information about possible function call optimizations
// and provides means to check its compatibilty with a given receiver.
// Could be initialized in two ways:
// 1. with an instance of JSFunction
// 2. with an API function represented by a FunctionTemplateInfo
class CallOptimization {
 public:
  CallOptimization(Isolate* isolate, Handle<Object> function);

  Context GetAccessorContext(Map holder_map) const;
  bool IsCrossContextLazyAccessorPair(Context native_context,
                                      Map holder_map) const;

  bool is_constant_call() const { return !constant_function_.is_null(); }

  Handle<JSFunction> constant_function() const {
    DCHECK(is_constant_call());
    return constant_function_;
  }

  // Returns {true} if the CallOptimization was initialized with an API
  // function, e.g. a property accessor or a JSFunction that can be interpreted
  // as such, i.e. one that has a C++ callback.
  bool is_simple_api_call() const { return is_simple_api_call_; }

  // Returns the {signature} of the API function, if defined.
  // See the comment in the FunctionTemplateInfo class for more info.
  Handle<FunctionTemplateInfo> expected_receiver_type() const {
    DCHECK(is_simple_api_call());
    return expected_receiver_type_;
  }

  // Returns the C++ handler invoked when calling the API function.
  Handle<CallHandlerInfo> api_call_info() const {
    DCHECK(is_simple_api_call());
    return api_call_info_;
  }

  enum HolderLookup { kHolderNotFound, kHolderIsReceiver, kHolderFound };

  // Performs a one step lookup for the so called "holder", i.e. the actual
  // object that owns the property, in case {is_simple_api_call} returns true.
  // Outputs the status in the {holder_lookup} and returns the corresponding
  // holder, if different than the receiver. The following values are possible:
  // 1. kHolderIsReceiver means the passed map belongs to an object
  //    instantiated by this function template. null is returned;
  // 2. kHolderFound means the lookup is performed on the global proxy.
  //    the prototype of {receiver_map} is returned;
  // 3. kHolderNotFound means the passed map doesn't belong to a JSObject
  //    or the object was not instantiated by this function template.
  Handle<JSObject> LookupHolderOfExpectedType(
      Handle<Map> receiver_map, HolderLookup* holder_lookup) const;

  // Check if the api holder is between the receiver and the holder.
  // Assumes {is_simple_api_call} is true.
  bool IsCompatibleReceiver(Handle<Object> receiver,
                            Handle<JSObject> holder) const;

  // Check if the api holder is between the receiver and the holder.
  // Assumes {is_simple_api_call} is true.
  bool IsCompatibleReceiverMap(Handle<Map> receiver_map,
                               Handle<JSObject> holder) const;

 private:
  void Initialize(Isolate* isolate, Handle<JSFunction> function);
  void Initialize(Isolate* isolate,
                  Handle<FunctionTemplateInfo> function_template_info);

  // Determines whether the given function can be called using the
  // fast api call builtin.
  void AnalyzePossibleApiFunction(Isolate* isolate,
                                  Handle<JSFunction> function);

  Handle<JSFunction> constant_function_;
  bool is_simple_api_call_;
  Handle<FunctionTemplateInfo> expected_receiver_type_;
  Handle<CallHandlerInfo> api_call_info_;
};
}  // namespace internal
}  // namespace v8

#endif  // V8_IC_CALL_OPTIMIZATION_H_
