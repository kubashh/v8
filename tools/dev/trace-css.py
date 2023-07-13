#!/usr/bin/env python3
# Copyright 2023 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
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
        if key == 'max':
          J[key] = max(J[key], value)
        elif key == 'min':
          J[key] = min(J[key], value)
        elif key == 'avg':
          pass
        else:
          J[key] += value
          if key in ['count', 'sum'] and 'count' in J and 'sum' in J \
             and J['count'] > 0:
            J['avg'] = J['sum'] / J['count']
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


def remove_histograms(J):
  if isinstance(J, dict):
    for key in J:
      if key == "histogram":
        J[key] = "omitted"
      else:
        remove_histograms(J[key])
  elif isinstance(J, list):
    for element in J:
      remove_histograms(element)


def process(f, args):
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
      sanity(payload)
      add_payload_to(J[reason], payload)
      J[reason]['count'] += 1
    except json.decoder.JSONDecodeError:
      # Only allow JSON errors on the last line of the input.
      json_error = True
  if not args.histograms:
    for reason in J:
      remove_histograms(J[reason])
  print(json.dumps(J, indent=2))


def sanity(S):
  assert S['primary']['count'] * 3 == S['secondary']['count'], \
    json.dumps(S, indent=2)
  assert S['secondary']['count'] == S['normal page']['count'] + \
    S['large page']['count'] + S['page not found']['count'], \
    json.dumps(S, indent=2)
  assert S['young from']['count'] == 0, \
    json.dumps(S, indent=2)
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
  assert goto_visitor == full + young, \
    json.dumps(S, indent=2)
  assert S['normal page']['count'] == S['not in young']['count'] + \
    S['young from']['count'] + S['already marked']['count'] + \
    S['iter forward']['count'], \
    json.dumps(S, indent=2)
  assert S['iter forward']['count'] == S['iter backward 1']['count'], \
    json.dumps(S, indent=2)


def main(args):
  if args.files:
    for filename in args.files:
      with open(filename, "rt") as f:
        process(f, args)
  else:
    process(sys.stdin)


if __name__ == "__main__":
  # Command line options.
  parser = argparse.ArgumentParser(
      description='Helper script for measuring the cost of CSS')
  parser.add_argument(
      '--histograms',
      action='store_true',
      help='show pointer histograms (default: no)')
  parser.add_argument('files', type=str, nargs='*', help='log file')
  args = parser.parse_args()
  # Call the main function.
  main(args)
