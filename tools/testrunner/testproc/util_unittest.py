#!/usr/bin/env python
# Copyright 2020 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from util import FixedSizeTopList
import unittest

class TestOrderedFixedSizeList(unittest.TestCase):
  def test_empty(self):
    ofsl = FixedSizeTopList(3)
    self.assertEqual(len(ofsl.asList()), 0)

  def test_12(self):
    ofsl = FixedSizeTopList(3)
    ofsl.add(1)
    ofsl.add(2)
    data = ofsl.asList()
    self.assertTrue(1 in data)
    self.assertTrue(2 in data)
    
  def test_4321(self):
    ofsl = FixedSizeTopList(3)
    ofsl.add(4)
    ofsl.add(3)
    ofsl.add(2)
    ofsl.add(1)
    data = ofsl.asList()
    self.assertTrue(2 in data)
    self.assertTrue(3 in data)
    self.assertTrue(4 in data)

  def test_withkey(self):
    ofsl = FixedSizeTopList(3,key=lambda x: x['val'])
    ofsl.add({'val':4, 'something':'good'})
    ofsl.add({'val':3, 'something':'else'})
    ofsl.add({'val':-1, 'something':'bad'})
    ofsl.add({'val':5, 'something':'extra'})
    ofsl.add({'val':0, 'something':'lame'})
    data = [e['something'] for e in ofsl.asList()]
    self.assertTrue('else' in data)
    self.assertTrue('good' in data)
    self.assertTrue('extra' in data)

  def test_withkeyreversed(self):
    ofsl = FixedSizeTopList(3,key=lambda x: x['val'], reversed=True)
    ofsl.add({'val':4, 'something':'good'})
    ofsl.add({'val':3, 'something':'else'})
    ofsl.add({'val':-1, 'something':'bad'})
    ofsl.add({'val':5, 'something':'extra'})
    ofsl.add({'val':0, 'something':'lame'})
    data = [e['something'] for e in ofsl.asList()]
    self.assertTrue('else' in data)
    self.assertTrue('lame' in data)
    self.assertTrue('bad' in data)


if __name__ == '__main__':
  unittest.main()
