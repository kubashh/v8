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
  let proto = Object.getPrototypeOf(obj);
    Object.getOwnPropertyNames(proto).forEach(name => {
      if (name !== 'constructor') {
        if (__isPropertyOfType(proto, name, type)) properties.push(name);
      }
    });
  return properties;
}
function __callRandomFunction(obj, seed, ...args) {
  let functions = __getProperties(obj, 'function');
  let random_function = functions[seed % functions.length];
    obj[random_function](...args);
}
const __v_0 = "en";
function __f_0(__v_3, __v_4) {
  return new Intl.NumberFormat(__v_3, {
    numberingSystem: __v_4
  });
}
  __v_2 = __f_0(`${__v_0}-u-nu-thai`, "arab");
  __callRandomFunction(__v_2, 605947, 1073741825, 1073741825,   );
