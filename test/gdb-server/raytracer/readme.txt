Test LLDB debugging of a Wasm module compiled with Clang + WASI-SDK.

* Compile LLVM/Clang/LLDB from the LLVM fork:
  https://github.com/paolosevMSFT/llvm-project/commits/Enable_WebAssembly_Debugging branch Enable_WebAssembly_Debugging.

* (On a Linux machine) compile wasi-sdk from:
  https://github.com/CraneStation/wasi-sdk

* Build v8 from this CL:
  https://chromium-review.googlesource.com/c/v8/v8/+/1800512/

* Compile raytrace to wasm:
  clang++ -gfull -O0 -std=c++2a --sysroot=g:\wasi-sysroot --target=wasm32-wasi raytracer.cpp -o ray.wasm -Wl,--no-entry,-allow-undefined,--export=raytrace -fno-exceptions

* Test with D8/LLDB:
  d8 --wasm-interpret-all --expose-wasm ray_test.js
  lldb 
    (gdb-remote 8765)
