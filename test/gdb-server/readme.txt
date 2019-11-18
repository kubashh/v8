Simple test for WebAssembly debugging with LLDB.

1. Compile with Clang, patched with https://reviews.llvm.org/D52634:

  clang -gfull -O0 -std=c++17 --target=wasm32 -nostdlib main.cc -o sort.wasm -Wl,--no-entry,--allow-undefined,--export=main

where:
  -gfull: emit full symbols in DWARF format
  -O0: disable optimizations


2. Strip DWARF debug data from the wasm module and put it into a separate ELF file with extension .dwo:

  python ..\wasm-to-dwo.py --dwo sort.dwo --wasm sort.wasm --module_url ~/test/sort/sort.wasm --symbols_url ~/test/sort/sort.dwo sort.wasm

where:
  --wasm specifies the name of the output wasm module, without symbols
  --dwo specifies the name of the generated symbols file, which should have extension ".dwo"
  --module_url specifies the path of the wasm module, which will be embedded in the "name" custom section of the module itself
  --symbols_url specifies the path of the symbols file, which will be embedded in the "SourceMappingURL" custom section


3. Run a test:

  d8 --wasm-interpret-all --expose-wasm sort_test.js

This will load the wasm module and will call its "main" function in an infinite loop.


4. Run LLDB (compiled with support for WebAssembly debugging)

  gdb-remote 8765 				Opens a debugging session to V8
  l						Shows source code
  bt						Displays the current call stack
  breakpoint set --file sort.h --line 11	Adds a breakpoint
  c						Continues execution
  var						Displays local variables