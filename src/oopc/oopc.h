// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OOPC_OOPC_H_
#define V8_OOPC_OOPC_H_

namespace v8 {
namespace oopc {

void Main(int argc, char* argv[]);

}  // namespace oopc
}  // namespace v8

int main(int argc, char* argv[]) {
  v8::oopc::Main(argc, argv);
  return 0;
}

#endif  // V8_OOPC_OOPC_H_
