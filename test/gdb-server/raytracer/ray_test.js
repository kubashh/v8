// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

let instance = null;
function go() {
    try {
        const memory = new WebAssembly.Memory({initial: 1024, maximum: 64 * 1024});

        const importObj = {
            env: {
                memory: memory,
                __memory_base: 1024,
                table: new WebAssembly.Table({
                    initial: 2,
                    maximum: 2,
                    element: 'anyfunc',
                }),
                consoleLog: function (s) { print(s); },
                out_result: function (offset, len) {
                    const strBuf = new Uint8Array(instance.exports.memory.buffer, offset, len);
                    writebuffer('out.ppm', strBuf);
                },
            }
        };

        let buf = readbuffer('ray.wasm');
        var module = new WebAssembly.Module(buf);
        instance = new WebAssembly.Instance(module, importObj);

        print('running...');
        instance.exports.raytrace(160, 120);
        print('done.');
    }
    catch (e) {
        print('*exception:* ' + e);
    }
}
go();
