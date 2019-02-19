// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Compare `in` operator on different types of arrays.

const size = 1e5;
var packed_smi = [];
var packed_double = [];
var packed_elements = [];
var holey_smi = new Array(size);
var holey_double = new Array(size);
var holey_elements = new Array(size);
var sparse_smi = new Array(size);
var sparse_double = new Array(size);
var sparse_elements = new Array(size);
var typed_uint8 = new Uint8Array(size);
var typed_int32 = new Int32Array(size);
var typed_float = new Float64Array(size);

for (let i = 0; i < size; ++i) {
    packed_smi[i] = i;
    packed_double[i] = i + 0.1;
    packed_elements[i] = "" + i;
    holey_smi[i] = i;
    holey_double[i] = i + 0.1;
    holey_elements[i] = "" + i;
    typed_uint8[i] = i % 0x100;
    typed_int32[i] = i;
    typed_float[i] = i + 0.1;
}

var sparse = 0;
for (let i = 0; i < size; i += 100) {
    ++sparse;
    sparse_smi[i] = i;
    sparse_double[i] = i + 0.1;
    sparse_elements[i] = "" + i;
}

// ----------------------------------------------------------------------------
// Benchmark: Packed SMI
// ----------------------------------------------------------------------------

function PackedSMI() {
    var cnt = 0;
    for (let i = 0; i < packed_smi.length; ++i) {
        if (i in packed_smi) ++cnt;
    }

    if (cnt != size) throw 666;
}

// ----------------------------------------------------------------------------
// Benchmark: Packed Double
// ----------------------------------------------------------------------------

function PackedDouble() {
    var cnt = 0;
    for (let i = 0; i < packed_double.length; ++i) {
        if (i in packed_double) ++cnt;
    }

    if (cnt != size) throw 666;
}

// ----------------------------------------------------------------------------
// Benchmark: Packed Elements
// ----------------------------------------------------------------------------

function PackedElements() {
    var cnt = 0;
    for (let i = 0; i < packed_elements.length; ++i) {
        if (i in packed_elements) ++cnt;
    }

    if (cnt != size) throw 666;
}

// ----------------------------------------------------------------------------
// Benchmark: Holey SMI
// ----------------------------------------------------------------------------

function HoleySMI() {
    var cnt = 0;
    for (let i = 0; i < holey_smi.length; ++i) {
        if (i in holey_smi) ++cnt;
    }

    if (cnt != size) throw 666;
}

// ----------------------------------------------------------------------------
// Benchmark: Holey Double
// ----------------------------------------------------------------------------

function HoleyDouble() {
    var cnt = 0;
    for (let i = 0; i < holey_double.length; ++i) {
        if (i in holey_double) ++cnt;
    }

    if (cnt != size) throw 666;
}

// ----------------------------------------------------------------------------
// Benchmark: Holey Elements
// ----------------------------------------------------------------------------

function HoleyElements() {
    var cnt = 0;
    for (let i = 0; i < holey_elements.length; ++i) {
        if (i in holey_elements) ++cnt;
    }

    if (cnt != size) throw 666;
}

// ----------------------------------------------------------------------------
// Benchmark: Sparse SMI
// ----------------------------------------------------------------------------

function SparseSMI() {
    var cnt = 0;
    for (let i = 0; i < sparse_smi.length; ++i) {
        if (i in sparse_smi) ++cnt;
    }

    if (cnt != sparse) throw 666;
}

// ----------------------------------------------------------------------------
// Benchmark: Sparse Double
// ----------------------------------------------------------------------------

function SparseDouble() {
    var cnt = 0;
    for (let i = 0; i < sparse_double.length; ++i) {
        if (i in sparse_double) ++cnt;
    }

    if (cnt != sparse) throw 666;
}

// ----------------------------------------------------------------------------
// Benchmark: Sparse Elements
// ----------------------------------------------------------------------------

function SparseElements() {
    var cnt = 0;
    for (let i = 0; i < sparse_elements.length; ++i) {
        if (i in sparse_elements) ++cnt;
    }

    if (cnt != sparse) throw 666;
}

// ----------------------------------------------------------------------------
// Benchmark: Typed Uint8
// ----------------------------------------------------------------------------

function TypedUint8() {
    var cnt = 0;
    for (let i = 0; i < typed_uint8.length; ++i) {
        if (i in typed_uint8) ++cnt;
    }

    if (cnt != size) throw 666;
}

// ----------------------------------------------------------------------------
// Benchmark: Typed Int32
// ----------------------------------------------------------------------------

function TypedInt32() {
    var cnt = 0;
    for (let i = 0; i < typed_int32.length; ++i) {
        if (i in typed_int32) ++cnt;
    }

    if (cnt != size) throw 666;
}

// ----------------------------------------------------------------------------
// Benchmark: Typed Float64
// ----------------------------------------------------------------------------

function TypedFloat64() {
    var cnt = 0;
    for (let i = 0; i < typed_float.length; ++i) {
        if (i in typed_float) ++cnt;
    }

    if (cnt != size) throw 666;
}

// ----------------------------------------------------------------------------
// Setup and Run
// ----------------------------------------------------------------------------

load('../base.js');

var success = true;

function PrintResult(name, result) {
  print(name + '-ArrayInOperator(Score): ' + result);
}

function PrintError(name, error) {
  PrintResult('Error: ' + name, error);
  success = false;
}

function CreateBenchmark(name, f) {
  new BenchmarkSuite(name, [1000], [ new Benchmark(name, false, false, 5, f) ]);
}

CreateBenchmark('PackedSMI', PackedSMI);
CreateBenchmark('PackedDouble', PackedDouble);
CreateBenchmark('PackedElements', PackedElements);
CreateBenchmark('HoleySMI', HoleySMI);
CreateBenchmark('HoleyDouble', HoleyDouble);
CreateBenchmark('HoleyElements', HoleyElements);
CreateBenchmark('SparseSMI', SparseSMI);
CreateBenchmark('SparseDouble', SparseDouble);
CreateBenchmark('SparseElements', SparseElements);
CreateBenchmark('TypedUint8', TypedUint8);
CreateBenchmark('TypedInt32', TypedInt32);
CreateBenchmark('TypedFloat64', TypedFloat64);

BenchmarkSuite.config.doWarmup = true;
BenchmarkSuite.config.doDeterministic = true;
BenchmarkSuite.RunSuites({NotifyResult: PrintResult, NotifyError: PrintError});
