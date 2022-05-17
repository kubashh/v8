#!perl

# Copyright 2022 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

use strict;
use warnings;
use feature 'say';

my $REPEAT = 20;
my @thresholds = (1, 8, 16, 32, 40, 48, 56, 64, 72, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384, 448, 512, 640, 768, 896, 1024);
#my @thresholds = (1, 32, 48, 96);

my %timings;
for (1 .. $REPEAT) {
  say "$_/$REPEAT...";
  for my $threshold (@thresholds) {
    # Setting kSIMDThreshold
    {
      local $^I = "";
      @ARGV = 'src/builtins/builtins-array-gen.cc';
      while (<>) {
        s/const int kSIMDThreshold = \K\d+/$threshold/;
        print;
      }
    }
    # Compiling v8
    system "autoninja -C out/x64.release";

    # Running benchmarks
    my $out = `out/x64.release/d8 --no-turbofan arr.js`;

    # Parsing outputs
    for (split /\n/, $out) {
      my ($inout, $size, $time) = /\[(\w+)\] size = (\d+) --> (\d+(?:\.\d+)?)/;
      push @{$timings{$inout}{$size}{$threshold}}, $time;
    }
  }
}

# Outputing results
for my $inout (keys %timings) {
  say "+-------------------------------+";
  say "|            $inout             |";
  say "+-------------------------------+\n";

  say "     |", join " | ", map { sprintf "%4s", $_ } @thresholds;
  say "-----+", join "-+-", map { "----" } @thresholds;
  for my $len (sort {$a <=> $b} keys %{$timings{$inout}}) {
    say sprintf("%4s |", $len),
      join " | ", map { sprintf "%4s", int_sum(@$_) } @{$timings{$inout}{$len}}{@thresholds};
  }
}



sub int_sum {
  my $tot = 0;
  $tot += $_ for @_;
  return int($tot);
}
