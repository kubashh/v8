# Copyright 2017 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import sys

args = ' '.join(sys.argv[1:])
print args
if 'berries' in args:
  sys.exit(1)
sys.exit(0)
