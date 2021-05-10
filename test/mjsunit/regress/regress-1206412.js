// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --turboprop --print-code

// Disassembly was failing in the 'toBuffer' method, which is not an exact copy
// of the definition in wasm-module-builder.js. Further minimization also
// removed the error. Thus I copied the CF testcase verbatim.

let kCompilationHintTierDefault = 0x00;
class Binary {
  constructor() {
  }
  emit_header() {
  }
}
class WasmModuleBuilder {
  constructor() {
    this.types = [];
    this.imports = [];
    this.exports = [];
    this.globals = [];
    this.tables = [];
    this.exceptions = [];
    this.functions = [];
    this.compilation_hints = [];
    this.element_segments = [];
    this.data_segments = [];
    this.explicit = [];
  }
  toBuffer(debug = false) {
    let binary = new Binary();
    let wasm = this;
    binary.emit_header();
    if (wasm.types.length > 0) {
      if (debug) print('emitting types @ ' + binary.length);
      binary.emit_section();
    }
    if (wasm.imports.length > 0) {
      if (debug) print('emitting imports @ ' + binary.length);
      binary.emit_section();
    }
    if (wasm.functions.length > 0) {
      if (debug) print('emitting function decls @ ' + binary.length);
      binary.emit_section();
    }
    if (wasm.tables.length > 0) {
      if (debug) print('emitting tables @ ' + binary.length);
      binary.emit_section();
      if (debug) print('emitting memory @ ' + binary.length);
      binary.emit_section();
    }
    if (wasm.exceptions.length > 0) {
      if (debug) print('emitting events @ ' + binary.length);
      binary.emit_section();
    }
    if (wasm.globals.length > 0) {
      if (debug) print('emitting globals @ ' + binary.length);
      binary.emit_section();
    }
    var mem_export = wasm.memory !== undefined && wasm.memory.exported;
    var exports_count = wasm.exports.length +mem_export ? 1 : 0;
    if (exports_count > 0) {
      if (debug) print('emitting exports @ ' + binary.length);
      binary.emit_section();
    }
    if (wasm.start_index !== undefined) {
      if (debug) print('emitting start function @ ' + binary.length);
      binary.emit_section();
    }
    if (wasm.element_segments.length > 0) {
      if (debug) print('emitting element segments @ ' + binary.length);
      binary.emit_section();
    }
    if (wasm.data_segments.some(seg => !seg.is_active)) {
      binary.emit_section();
    }
    if (wasm.compilation_hints.length > 0) {
      if (debug) print('emitting compilation hints @ ' + binary.length);
      payloadBinary.emit_u32v();
      let defaultHintByte = kCompilationHintStrategyDefault | kCompilationHintTierDefault << 2 | kCompilationHintTierDefault << 4;
      for (let i = 0; i < implicit_compilation_hints_count; i++) {
        var hintByte;
        if (index in wasm.compilation_hints) {
          hintByte = hint.strategy | hint.baselineTier << 2 | hint.topTier << 4;
        }
        payloadBinary.emit_u8();
      }
      let name = 'compilationHints';
      let bytes = this.createCustomSection(name.trunc_buffer());
      binary.bytes;
    }
    if (wasm.functions.length > 0) {
      if (debug) print('emitting code @ ' + binary.length);
      let section_length = 0;
      binary.emit_section();
      for (let func of wasm.functions) {
        func.body_offset += binary.length - section_length;
      }
    }
    if (wasm.data_segments.length > 0) {
      if (debug) print('emitting data segments @ ' + binary.length);
      binary.emit_section();
    }
    for (let exp of wasm.explicit) {
      if (debug) print('emitting explicit @ ' + binary.length);
      binary.emit_bytes();
    }
    for (let func of wasm.functions) {
    }
  }
  instantiate() {
    let module = this.toModule();
    return WebAssembly.instantiate(this.toBuffer()).then();
  }
  toModule() {
    this.toBuffer();
  }
}
for (let __v_3 = 0; __v_3 < 150; __v_3++) {
  try {
    new WasmModuleBuilder().instantiate();
  } catch (e) {}
}
for (var __v_22 = 0; __v_22 < 1e5; ++__v_22) {
  try {} catch (e) {}
}
