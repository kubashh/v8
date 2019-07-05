// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

let min_fields = 1015;
let max_fields = 1025;

let static_fields_src = "";
let instance_fields_src = "";
for (let i = 0; i < min_fields; i++) {
  static_fields_src += "  static f" + i + "() {}\n";
  instance_fields_src += "  g" + i + "() {}\n";
}

for (let i = min_fields; i < max_fields; i++) {
  let src = "class A {\n" + static_fields_src + "}\n";
  eval(src);
  src = "class B {\n" + instance_fields_src + "}\n";
  eval(src);
  static_fields_src += "  static f" + i + "() {}\n";
  instance_fields_src += "  g" + i + "() {}\n";
}
