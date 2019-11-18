// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

let instance1 = null;
let instance2 = null;
let rgb_to_grayscale = null;
let ppm_to_grayscale = null;
let rgb_to_mono = null;

function go() {
    try {
        const importObj0 = {
            env: {
                memory: { initial: 2, maximum: 2 },
                __memory_base: 1024,
                table: new WebAssembly.Table({ initial: 2, maximum: 2, element: 'anyfunc', }),
            }
        };
        var module0 = new WebAssembly.Module(readbuffer('rgb_mono.wasm'));
        instance0 = new WebAssembly.Instance(module0, importObj0);
        rgb_to_mono = instance0.exports.rgb_to_mono;

        const importObj1 = {
            env: {
                memory: { initial: 1024, maximum: 1024 },
                __memory_base: 1024,
                table: new WebAssembly.Table({ initial: 2, maximum: 2, element: 'anyfunc', }),
                rgb_to_grayscale: function(offset, len) {
                    const strBuf = new Uint8Array(instance1.exports.memory.buffer, offset, len);
                    for (let i = 0; i < len; i += 3) {
                        let r = strBuf[i];
                        let g = strBuf[i + 1];
                        let b = strBuf[i + 2];
                        let grey = rgb_to_mono(r, g, b);
                        strBuf[i] = strBuf[i + 1] = strBuf[i + 2] = grey;
                    }
                },
                ppm_to_grayscale: function(offset, len) { ppm_to_grayscale(offset, len); },
                consoleLog: function (val) { },
                out_result: function (offset, len) {
                },
            }
        };
        var module1 = new WebAssembly.Module(readbuffer('ray.wasm'));
        instance1 = new WebAssembly.Instance(module1, importObj1);

        const importObj2 = {
            env: {
                memory: instance1.exports.memory,
                __memory_base: 1024,
                table: new WebAssembly.Table({ initial: 2, maximum: 2, element: 'anyfunc', }),
                consoleLog: function (val) { },
            }
        };
        var module2 = new WebAssembly.Module(readbuffer('image_helper.wasm'));
        instance2 = new WebAssembly.Instance(module2, importObj2);
        rgb_to_grayscale = instance2.exports.rgb_to_grayscale;
        ppm_to_grayscale = instance2.exports.ppm_to_grayscale;

        instance1.exports.raytrace(12, 6);
    }
    catch (e) {
        print('*exception:* ' + e + '\n' + e.stack);
    }
}
go();
