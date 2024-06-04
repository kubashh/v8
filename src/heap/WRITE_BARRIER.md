# V8 Write Barrier - Readme

V8 uses a write barrier to inform the GC about changes to the heap by the mutator.
The write barrier is required for multiple purposes:
* Records old-to-new referencs for the generational GC to work.
* During marking it prevents black-to-old references during incremental/concurrent marking.
* During marking it records old-to-old references (pointers to objects on evacuation candidates)

The generational barrier is always enabled, while the other barriers are only enabled while incremental/concurrent marking is running.

# Overview
The fast path of the write barrier has different implementations in all compilers and C++.
CSA and the interpreter do not have their own barrier but reuse Turbofan's write barrier.
While implementations differ, the general idea is still the same for all of them.
For a store like `host.field = value` the write barrier will be organized as follows:

## Fast path
Checks whether the host object has the `POINTERS_FROM_HERE_ARE_INTERESTING` flag set on its page header.
If yes, the write barrier will jump into an out-of-line (or deferred) code path.
Otherwise regular execution continues.
This check will be skipped if value is a Smi.

Turbofan implements the fast path in [code-generator-x64.cc](https://source.chromium.org/chromium/chromium/src/+/main:v8/src/compiler/backend/x64/code-generator-x64.cc?q=kArchAtomicStoreWithWriteBarrier) for the `kArchAtomicStoreWithWriteBarrier` instruction opcode.
Maglev implements the fast path in [maglev-assembler-x64.cc](https://source.chromium.org/chromium/chromium/src/+/main:v8/src/maglev/maglev-assembler.cc?q=MaglevAssembler::CheckAndEmitDeferredWriteBarrier) in the [StoreTaggedFieldWithWriteBarrier](https://source.chromium.org/chromium/chromium/src/+/main:v8/src/maglev/maglev-ir.cc?q=StoreTaggedFieldWithWriteBarrier::GenerateCode) node.
Sparkplug implements it in [MacroAssembler::RecordWrite(https://source.chromium.org/chromium/chromium/src/+/main:v8/src/codegen/x64/macro-assembler-x64.cc?q=MacroAssembler::RecordWrite%28).
For C++ the fast path is implemented in [CombinedWriteBarrierInternal](https://source.chromium.org/chromium/chromium/src/+/main:v8/src/heap/heap-write-barrier-inl.h?q=CombinedWriteBarrierInternal).

## Deferred code path
Checks whether the value object has the `POINTERS_TO_HERE_ARE_INTERESTING` flag set on its page header.
If yes, the write barrier will call the builtin function which implements the slow path.
Otherwise the deferred code jumps back to the regular instruction stream.

Turbofan implements the deferred code path in the [OutOfLineRecordWrite](https://source.chromium.org/chromium/chromium/src/+/main:v8/src/compiler/backend/x64/code-generator-x64.cc?q=OutOfLineRecordWrite) class.
Maglev emits this code path using [MakeDeferredCode](https://source.chromium.org/chromium/chromium/src/+/main:v8/src/maglev/maglev-assembler.cc?q=MaglevAssembler::CheckAndEmitDeferredWriteBarrier) in the same function as the fast path.
Sparkplug is the only compiler which does not emit this check in some out of line code path but instead emits them directly inline in [MacroAssembler::RecordWrite](https://source.chromium.org/chromium/chromium/src/+/main:v8/src/codegen/x64/macro-assembler-x64.cc?q=MacroAssembler::RecordWrite%28).

## Slow path
The slow path of the barrier is shared between all compilers and call sites.
In the generational barrier the field/slot is simply recorded in the old-to-new slot set.
During incremental/concurrent marking it also marks the `value` object and may record in the old-to-old remembered set.
This part of the barrier is implemented in CSA in [builtins-internal-gen.cc](https://source.chromium.org/chromium/chromium/src/+/main:v8/src/builtins/builtins-internal-gen.cc?q=WriteBarrierCodeStubAssembler) as the [RecordWriteXXX](https://source.chromium.org/chromium/chromium/src/+/main:v8/src/builtins/builtins-internal-gen.cc?q=RecordWriteSaveFp) builtins.

## C++ slow path
Inserting into the slot set in the RecordWrite builtin may need to `malloc()` memory.
The builtin therefore may call into C++ for this operation.
The C++ function for this is [Heap::InsertIntoRememberedSetFromCode](https://source.chromium.org/chromium/chromium/src/+/main:v8/src/heap/heap.cc?q=Heap::InsertIntoRememberedSetFromCode).

# Indirect pointer barrier
V8 also needs a special barrier for pointers into the trusted space with the sandbox.
This barrier is only implemented for 64-bit architectures and only used in builtins written in either CSA or assembler.

The Turbofan indirect pointer write barrier is implemented as a separate instruction opcode [kArchStoreIndirectWithWriteBarrier](https://source.chromium.org/chromium/chromium/src/+/main:v8/src/compiler/backend/x64/code-generator-x64.cc?q=kArchStoreIndirectWithWriteBarrier).
The deferred code path is implemented in the [OutOfLineRecordWrite](https://source.chromium.org/chromium/chromium/src/+/main:v8/src/compiler/backend/x64/code-generator-x64.cc?q=OutOfLineRecordWrite) class.

Assembler builtins can emit this barrier using [MacroAssembler::RecordWrite](https://source.chromium.org/chromium/chromium/src/+/main:v8/src/codegen/x64/macro-assembler-x64.cc?q=MacroAssembler::RecordWrite%28).
