#!/usr/bin/env python
# Copyright 2020 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import bisect


class OrderedFixedSizeList():
  def __init__(self, size, key=None, reversed=False):
    self.size = size
    key = key or (lambda x: x)
    if reversed:
      self.key = lambda x: - key(x)
    else:
      self.key = key
    self.keys = []
    self.data = []
    if reversed:
      self.bisect = bisect.bisect_left
    else:
      self.bisect = bisect.bisect_right

  def add(self, elem):
    elem_k = self.key(elem)
    idx = bisect.bisect_left(self.keys, elem_k)
    self.keys.insert(idx, elem_k)
    self.data.insert(idx, elem)
    if len(self.keys) > self.size:
      del self.keys[-1]
      del self.data[-1]
  
  def asList(self):
    return self.data
