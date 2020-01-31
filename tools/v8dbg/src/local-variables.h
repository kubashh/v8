// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_WIN_V8DBG_SRC_FRAME_H_
#define TOOLS_WIN_V8DBG_SRC_FRAME_H_

#include <comutil.h>
#include <wrl/implements.h>

#include "tools/v8dbg/base/dbgext.h"

// An implementation of the property accessor for the "LocalVariables" or
// "Parameters" property on Debugger.Models.StackFrame. This allows us to modify
// the variables shown in each frame.
class V8LocalVariables
    : public WRL::RuntimeClass<
          WRL::RuntimeClassFlags<WRL::RuntimeClassType::ClassicCom>,
          IModelPropertyAccessor> {
 public:
  V8LocalVariables(WRL::ComPtr<IModelPropertyAccessor> original,
                   bool is_parameters);
  ~V8LocalVariables() override;

  IFACEMETHOD(GetValue)
  (PCWSTR key, IModelObject* context, IModelObject** value);
  IFACEMETHOD(SetValue)(PCWSTR key, IModelObject* context, IModelObject* value);

 private:
  // The built-in accessor which we are overriding.
  WRL::ComPtr<IModelPropertyAccessor> original_;
  // Whether this is for Parameters rather than LocalVariables.
  bool is_parameters_;
};

#endif
