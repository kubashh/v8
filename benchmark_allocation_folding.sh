#!/bin/sh
# Build with instrumentation.
autoninja -C out/release cppgc_basic_benchmarks
# Collect profiling feedback.
./out/release/cppgc_basic_benchmarks --benchmark_filter="Allocate/TwoUnfolded|Allocate/Folded"
llvm-profdata merge *.profraw -o folded.profdata
rm *.profraw
# Collect profiling feedback.
autoninja -C out/release_with_profile cppgc_basic_benchmarks
./out/release_with_profile/cppgc_basic_benchmarks --benchmark_filter="Allocate/TwoUnfolded|Allocate/Folded"
