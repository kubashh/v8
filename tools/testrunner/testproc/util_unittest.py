#!/usr/bin/env python
# Copyright 2020 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from util import OrderedFixedSizeList
import unittest

class TestOrderedFixedSizeList(unittest.TestCase):
  def test_empty(self):
    ofsl = OrderedFixedSizeList(3)
    self.assertEqual(len(ofsl.asList()), 0)

  def test_12(self):
    ofsl = OrderedFixedSizeList(3)
    ofsl.add(1)
    ofsl.add(2)
    self.assertEqual(ofsl.asList(), [1,2])
    
  def test_4321(self):
    ofsl = OrderedFixedSizeList(3,reversed=True)
    ofsl.add(4)
    ofsl.add(3)
    ofsl.add(2)
    ofsl.add(1)
    self.assertEqual(ofsl.asList(), [4,3,2])

  def test_withkey(self):
    ofsl = OrderedFixedSizeList(3,key=lambda x: x['val'], reversed=True)
    ofsl.add({'val':4, 'something':'good'})
    ofsl.add({'val':3, 'something':'else'})
    ofsl.add({'val':-1, 'something':'bad'})
    ofsl.add({'val':5, 'something':'extra'})
    ofsl.add({'val':0, 'something':'lame'})
    self.assertEqual([e['something'] for e in ofsl.asList()], ['extra', 'good', 'else'])


if __name__ == '__main__':
  unittest.main()
