// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

function go() {
    const importObj = {
        env: {
            memory: new WebAssembly.Memory({initial: 256, maximum: 256}),
            __memory_base: 1024,
            table: new WebAssembly.Table({
                initial: 2,
                maximum: 2,
                element: 'anyfunc',
            }),
            consoleLog: function (s) { print(s);
            }
        }
    };

    try {
        let buf = readbuffer('sort.wasm');
        var module = new WebAssembly.Module(buf);
        let instance = new WebAssembly.Instance(module, importObj);
        var main = instance.exports.main;

        print('running...');
        while (true) {
            main();
        }
    }
    catch (e) {
        print('*exception:* ' + e);
    }
}
go();
