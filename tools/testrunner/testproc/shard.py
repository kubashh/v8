# Copyright 2018 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from . import base


class ShardProc(base.TestProcFilter):
  """Processor distributing tests between shards.
  It simply passes every n-th test. To be deterministic it has to be placed
  before all processors that generate tests dynamically.
  """
  def __init__(self, myid, shards_count, variants_count):
    """
    Args:
      myid: id of the shard within [0; shards_count - 1]
      shards_count: number of shards
    """
    super(ShardProc, self).__init__()

    assert myid >= 0 and myid < shards_count

    self._myid = myid
    self._shards_count = shards_count

  def _filter(self, test):
    should_skip = (test.id % self._shards_count) != self._myid
    return should_skip
