// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --harmony-intl-number-format-v3

function __isPropertyOfType(obj, name, type) {
    desc = Object.getOwnPropertyDescriptor(obj, name);
  return typeof type === 'undefined' || typeof desc.value === type;
}
function __getProperties(obj, type) {
  let properties = [];
  for (let name of Object.getOwnPropertyNames(obj)) {
  }
  let proto = Object.getPrototypeOf(obj);
    Object.getOwnPropertyNames(proto).forEach(name => {
      if (name !== 'constructor') {
        if (__isPropertyOfType(proto, name, type)) properties.push(name);
      }
    });
  return properties;
}
function* __getObjects() {
  for (let obj_name of obj_names) {
  }
}
function __getRandomObject() {
}
function __callRandomFunction(obj, seed, ...args) {
  let functions = __getProperties(obj, 'function');
  let random_function = functions[seed % functions.length];
    obj[random_function](...args);
}
  __v_0 = Reflect.construct(Intl.DateTimeFormat, []);
      __callRandomFunction(__v_0, 764167, -5e-324 , 607603, -1073741824);
