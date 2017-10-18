# Copyright 2017 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""The same as dump_build_config.py but for gyp legacy."""
# TODO(machenbach): Remove this when gyp is deprecated.

import json
import os
import sys

assert len(sys.argv) > 1


GYP_GN_CONVERSION = {
  'is_component_build': {
    '"shared_library"': 'true',
    '"static_library"': 'false',
  },
  'is_debug': {
    '"Debug"': 'true',
    '"Release"': 'false',
  },
}

DEFAULT_CONVERSION ={
  '0': 'false',
  '1': 'true',
  '"ia32"': '"x86"',
}

def gyp_to_gn(key, value):
  return GYP_GN_CONVERSION.get(key, DEFAULT_CONVERSION).get(value, value)

def as_json(kv):
  assert '=' in kv
  k, v = kv.split('=', 1)
  return k, json.loads(gyp_to_gn(k, v))

with open(sys.argv[1], 'w') as f:
  json.dump(dict(as_json(kv) for kv in sys.argv[2:]), f)
