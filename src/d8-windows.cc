// Copyright 2009 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/d8.h"


namespace v8 {


void Shell::AddOSMethods(Isolate* isolate, Local<ObjectTemplate> os_templ) {}

char* Shell::ReadCharsFromTcpPort(const char* name, int* size_out) {
  // TODO(leszeks): No reason this shouldn't exist on windows.
  return nullptr;
}

Local<String> Shell::ReadFromStdin(Isolate* isolate) {
  printf("d8> ");
  static const int kBufferSize = 256;
  char buffer[kBufferSize];
  Local<String> accumulator =
      String::NewFromUtf8(isolate, "", NewStringType::kNormal).ToLocalChecked();
  int length;
  while (true) {
    // Continue reading if the line ends with an escape '\\' or the line has
    // not been fully read into the buffer yet (does not end with '\n').
    // If fgets gets an error, just give up.
    char* input = nullptr;
    input = fgets(buffer, kBufferSize, stdin);
    if (input == nullptr) return Local<String>();
    length = static_cast<int>(strlen(buffer));
    if (length == 0) {
      return accumulator;
    } else if (buffer[length - 1] != '\n') {
      accumulator = String::Concat(
          accumulator,
          String::NewFromUtf8(isolate, buffer, NewStringType::kNormal, length)
              .ToLocalChecked());
      printf("... ");
    } else if (length > 1 && buffer[length - 2] == '\\') {
      buffer[length - 2] = '\n';
      accumulator = String::Concat(
          accumulator, String::NewFromUtf8(isolate, buffer,
                                           NewStringType::kNormal, length - 1)
                           .ToLocalChecked());
    } else {
      return String::Concat(
          accumulator, String::NewFromUtf8(isolate, buffer,
                                           NewStringType::kNormal, length - 1)
                           .ToLocalChecked());
    }
  }
}

}  // namespace v8
