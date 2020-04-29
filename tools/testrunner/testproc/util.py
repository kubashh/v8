#!/usr/bin/env python
# Copyright 2020 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import heapq

class FixedSizeTopList():
  """Utility collection for gathering a fixed number of elements with the
  bigest value for the given key. It employs a Heap from which we pop the 
  smallest element when the collection is 'full'
  
  If you need a reversed behaviour (collect min values) just provide an 
  inverse key."""

  def __init__(self, size, key=None):
    self.size = size
    self.key = key or (lambda x: x)
    self.data = []

  def add(self, elem):
    elem_k = self.key(elem)
    heapq.heappush(self.data, (elem_k, elem))
    if len(self.data) > self.size:
      heapq.heappop(self.data)

  def as_list(self):
    return sorted([rec for (_, rec) in self.data], key=self.key, reverse=True)
