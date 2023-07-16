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
    if 'count' in P:
      for key, value in P.items():
        assert key not in J or isinstance(J[key], (int, float))
        assert isinstance(value, (int, float))
        if key not in J:
          J[key] = value
        elif key == 'max':
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
      for key, value in P.items():
        if key not in J:
          if isinstance(value, (int, float)):
            J[key] = {
                'count': 1,
                'max': value,
                'min': value,
                'sum': value,
                'avg': value
            }
          else:
            J[key] = {}
            add_payload_to(J[key], value)
        elif isinstance(value, (int, float)):
          J[key]['count'] += 1
          J[key]['max'] = max(J[key]['max'], value)
          J[key]['min'] = min(J[key]['max'], value)
          J[key]['sum'] += value
          J[key]['avg'] = J[key]['sum'] / J[key]['count']
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
      if key == 'histogram':
        J[key] = 'omitted'
      else:
        remove_histograms(J[key])
  elif isinstance(J, list):
    for element in J:
      remove_histograms(element)


def process(workbook, filename, f, args):
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
      J[reason] = {}
    payload = m.group(5)
    try:
      payload = json.loads(payload)
      sanity(payload)
      add_payload_to(J[reason], payload)
    except json.decoder.JSONDecodeError:
      # Only allow JSON errors on the last line of the input.
      json_error = True
  if not args.histograms:
    for reason in J:
      remove_histograms(J[reason])
  if workbook is None:
    print(json.dumps(J, indent=2))
  else:
    sheet = filename.split('/')[0]
    worksheet = workbook.add_worksheet(sheet)
    print_to_worksheet(worksheet, J)


def sanity(S):
  assert S['primary']['total'] * 3 == S['secondary']['total'], \
    json.dumps(S, indent=2)
  assert S['secondary']['total'] == S['normal page']['total'] + \
    S['large page']['total'] + S['page not found']['total'], \
    json.dumps(S, indent=2)
  assert S['young from']['total'] == 0, \
    json.dumps(S, indent=2)
  not_goto_visitor = S['page not found']['total'] + \
    S['free space']['total'] + S['not in young']['total'] + \
    S['young from']['total'] + S['already marked']['total']
  goto_visitor = S['secondary']['total'] - not_goto_visitor
  full = S['full, should not mark']['total'] + \
    S['full, already marked']['total'] + \
    S['full, not already marked']['total']
  young = S['young, should not mark']['total'] + \
    S['young, already marked']['total'] + \
    S['young, not already marked']['total']
  assert goto_visitor == full + young, \
    json.dumps(S, indent=2)
  assert S['normal page']['total'] == S['not in young']['total'] + \
    S['young from']['total'] + S['already marked']['total'] + \
    S['iter forward']['count'], \
    json.dumps(S, indent=2)
  assert S['iter forward']['count'] == S['iter backward 1']['count'], \
    json.dumps(S, indent=2)


def print_to_worksheet(worksheet, J):
  assert len(J) == 1, 'CSS for more reasons than GC!'
  assert 'GC' in J
  J = J['GC']
  headers = ['count', 'max', 'min', 'sum', 'avg']
  for i, header in enumerate(headers, 2):
    worksheet.write(0, i, header)
  row = 1

  def add_category(main, category, S):
    nonlocal row
    worksheet.write(row, 0, main)
    worksheet.write(row, 1, category)
    for i, header in enumerate(headers, 2):
      if header in S:
        worksheet.write(row, i, S[header])
    row += 1

  for key in J:
    if 'count' in J[key]:
      assert key[:5] == 'iter '
      category = key[5:]
      add_category('iterations', category, J[key])
    else:
      for category in J[key]:
        add_category(key, category, J[key][category])


def main(args):
  if args.out_xlsx:
    import xlsxwriter
    workbook = xlsxwriter.Workbook(args.out_xlsx)
  else:
    workbook = None
  if args.files:
    for filename in args.files:
      with open(filename, 'rt') as f:
        process(workbook, filename, f, args)
  else:
    process(workbook, 'stdin', sys.stdin, args)
  if workbook is not None:
    workbook.close()


if __name__ == '__main__':
  # Command line options.
  parser = argparse.ArgumentParser(
      description='Helper script for measuring the cost of CSS')
  parser.add_argument(
      '--histograms',
      action='store_true',
      help='show pointer histograms (default: no)')
  parser.add_argument(
      'files', type=str, nargs='*', help='log files containing the statistics')
  parser.add_argument(
      '--out-xlsx', action='store', type=str, help='output xlsx file')
  args = parser.parse_args()
  # Call the main function.
  main(args)
