#!/usr/bin/env python3
# Copyright 2023 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import json
import re
import sys


def add_payload_to(J, P):
  if isinstance(P, dict):
    assert isinstance(J, dict)
    for key, value in P.items():
      if key not in J:
        J[key] = value
      elif isinstance(J[key], (int, float)):
        assert isinstance(value, (int, float))
        if key in ['count', 'unique', 'sum']:
          J[key] += value
          if 'count' in J and J['count'] > 0 and 'sum' in J:
            J['avg'] = J['sum'] / J['count']
        elif key == 'max':
          J[key] = max(J[key], value)
        elif key == 'min':
          J[key] = min(J[key], value)
        else:
          assert key == 'avg'
      else:
        add_payload_to(J[key], value)
  else:
    assert isinstance(P, list), f'P={P}, J={J}'
    assert isinstance(J, list)
    assert len(P) == len(J)
    for i in range(len(J)):
      if isinstance(J[i], (int, float)):
        assert isinstance(P[i], (int, float))
        J[i] += P[i]
      else:
        add_payload_to(j, p)


def process(f):
  reStatsLine = re.compile(r'^\[([0-9A-Fa-fx]*):([0-9A-Fa-fx]*)\]' +
                           r'[ \t]+([0-9]+) ms: ' +
                           r'CSS statistics ([^:]*): (.*)$')
  J = {}
  json_error = False
  for line in f:
    assert not json_error
    m = reStatsLine.match(line)
    if m is None:
      continue
    reason = m.group(4)
    if reason not in J:
      J[reason] = {'count': 0}
    payload = m.group(5)
    try:
      payload = json.loads(payload)
      add_payload_to(J[reason], payload)
      J[reason]['count'] += 1
    except json.decoder.JSONDecodeError:
      # Only allow JSON errors on the last line of the input.
      json_error = True
  print(json.dumps(J, indent=2))
  sanity(J)


def sanity(J):
  for reason, S in J.items():
    assert S['primary']['count'] * 3 == S['secondary']['count']
    assert S['secondary']['count'] == S['normal page']['count'] + \
      S['large page']['count'] + S['page not found']['count']
    assert S['young from']['count'] == 0
    not_goto_visitor = S['page not found']['count'] + \
      S['free space']['count'] + S['not in young']['count'] + \
      S['young from']['count'] + S['already marked']['count']
    goto_visitor = S['secondary']['count'] - not_goto_visitor
    full = S['full, should not mark']['count'] + \
      S['full, already marked']['count'] + \
      S['full, not already marked']['count']
    young = S['young, should not mark']['count'] + \
      S['young, already marked']['count'] + \
      S['young, not already marked']['count']
    assert goto_visitor == full + young
    assert S['normal page']['count'] == S['not in young']['count'] + \
      S['young from']['count'] + S['already marked']['count'] + \
      S['iter forward']['count']
    assert S['iter forward']['count'] == S['iter backward 1']['count']


if __name__ == '__main__':
  if len(sys.argv) == 1:
    process(sys.stdin)
  else:
    for filename in sys.argv[1:]:
      with open(filename, "rt") as f:
        process(f)
