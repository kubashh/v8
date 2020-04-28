#!/usr/bin/env python
# Copyright 2020 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import heapq

class FixedSizeTopList():
  def __init__(self, size, key=None, reversed=False):
    self.size = size
    key = key or (lambda x: x)
    if reversed:
      self.key = lambda x: - key(x)
    else:
      self.key = key
    self.data = []

  def add(self, elem):
    elem_k = self.key(elem)
    heapq.heappush(self.data, (elem_k, elem))
    if len(self.data) > self.size:
      heapq.heappop(self.data)

  def asList(self):
    return [rec for (_, rec) in self.data]
