// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_EXTENSIONS_CONSOLE_EXTENSION_H_
#define V8_EXTENSIONS_CONSOLE_EXTENSION_H_

#include "include/v8-extension.h"
#include "include/v8-local-handle.h"
#include "src/base/strings.h"

namespace v8 {

template <typename T>
class FunctionCallbackInfo;

namespace internal {

class ConsoleExtension : public v8::Extension {
 public:
  ConsoleExtension() : v8::Extension("v8/console", kSource) {
    set_auto_enable(true);
  }
  v8::Local<v8::FunctionTemplate> GetNativeFunctionTemplate(
      v8::Isolate* isolate, v8::Local<v8::String> name) override;

 private:
  static const char* const kSource;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_EXTENSIONS_CONSOLE_EXTENSION_H_
