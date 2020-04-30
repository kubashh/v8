#!/usr/bin/env python
# Copyright 2020 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""
A collection of helper functions for transforming text proto files to
other formats.
"""

import pprint
import re
import sys

MULTILINE_PLACEHOLDER = "MULTILINE_PLACEHOLDER"

block_matcher = re.compile(r"^([\w]+?) \{\n(.*)$", re.DOTALL)
props_regexp = r"^([\w]+)\: \"?(.*?)\"?$"
props_matcher = re.compile(props_regexp)
multiline_value_matcher = re.compile(r"<<END\n(.+?)\n\s*END\n", re.DOTALL)

def prototext2dict(proto_text):
  multiline_values = []
  for match in multiline_value_matcher.finditer(proto_text):
    multiline_values.append(match.group(1))
  proto_text = re.sub(multiline_value_matcher, MULTILINE_PLACEHOLDER+"\n", proto_text)
  return _prototext2dict(proto_text, multiline_values)

def _prototext2dict(proto_text, multiline_values):
  proto_text = remove_comments(proto_text)
  props = extract_props(proto_text, multiline_values)
  proto_text = remove_prop_lines(proto_text)
  for block in split_by_eoblock(proto_text):
    match = block_matcher.match(block.strip())
    if match:
      block_name = match.group(1)
      block_text = allign_to_left(match.group(2))
      block_props = _prototext2dict(block_text, multiline_values)
      collect_multi_value(props, block_name, block_props)
  return props

def extract_props(proto_text, multiline_values):
  props = dict()
  for l in proto_text.splitlines():
    match = props_matcher.match(l)
    if match:
      key = match.group(1)
      value = match.group(2)
      if value == MULTILINE_PLACEHOLDER:
        value = multiline_values.pop(0)
      collect_multi_value(props, key, value)
  return props

def collect_multi_value(_dict, key, value):
  _dict.setdefault(key, []).append(value)

def split_by_eoblock(text):
  r= re.split(r'^}\n', text.strip(), flags=re.MULTILINE)
  return r

def allign_to_left(text):
  return re.sub(r"^  ", "", text, flags=re.MULTILINE)

def remove_prop_lines(text):
  return re.sub(props_regexp, "", text, flags=re.MULTILINE).strip()

def remove_comments(text):
  return re.sub(r"^#.*$", "", text, flags=re.MULTILINE).strip()

def print_dict(d):
  pp = pprint.PrettyPrinter(indent=2)
  pp.pprint(d)

def read_proto_file(file_name):
  with open(file_name , 'r') as f:
    cfg_dict = prototext2dict(f.read())
    return cfg_dict






def buildbucket2star():
  cfg = read_proto_file('cr-buildbucket.cfg')
  mixins = process_mixins(cfg)
  builders = process_builders(cfg)
  collapse_builder_mixins(builders, mixins)
  collapse_builder_recipe_props(builders)
  print(write_acls(cfg))
  print(write_buckets(cfg))
  print(write_builders(builders))

def write_acls(cfg):
  return "\n".join([
    "acl_set_%s = [\n%s\n]" % (aset['name'][0], write_acls_entries(aset['acls']))
    for aset in cfg['acl_sets']
  ])

def write_acls_entries(acls):
  return ",\n".join([
    "  acl.entry(\n%s\n  )" % write_acls_entry(acl)
    for acl in acls
  ])

acl_roles = {
  'READER':'acl.BUILDBUCKET_READER',
  'SCHEDULER': 'acl.SCHEDULER_OWNER'
}
def write_acls_entry(acl):
  return "    %s,\n    groups=%s,\n    users=%s" % (
    "["+ ",".join([acl_roles[role] for role in acl['role']]) + "]",
    acl.get('groups', None),
    acl.get('identity', None),
  )

def write_buckets(cfg):
  return "\n".join([
    "luci.bucket(name=\"%s\", acls=acl_set_%s)\n" % (buck['name'][0].split('.')[-1], buck['acl_sets'][0])
    for buck in cfg['buckets']
  ])

def write_acl_names(acls):
  return "[" + ",".join(["acl_set_%s" % a for a in acls]) + "]" 

def write_builders(builders):
  return "\n".join(["""luci.builder(
  name=\"%s\",
  bucket=\"%s\",
  executable=%s,
  properties=%s,
  service_account=\"%s\",
  dimensions=%s,
  swarming_tags=%s,
  execution_timeout=%s * time.second,
  build_numbers=%s,
  caches=%s,
  priority=%s,
)""" % (
      name, 
      b['bucket'].split('.')[-1],
      write_executable(b['recipe']),
      b['recipe'].get('properties', {}),
      b['service_account'][0],
      list2dict(b['dimensions']),
      b['swarming_tags'],
      re.sub(r"^(\d*).*$", r"\1",  b['execution_timeout_secs'][0]),
      b.get('build_numbers', ['NO'])[0] == 'YES',
      write_caches(b.get('caches', [])),
      b.get('priority', [None])[0],
    ) 
    for name, b in builders.items()
  ])

def list2dict(l):
  r = dict()
  for e in l:
    s = e.split(':')
    r.update({s[0]:s[1]})
  return r

def dump(d):
  for s  in "name cipd_package cipd_version properties".split(" "):
    if s in d:
      del d[s]
  if d:
    print_dict(d)
  return str(d)

def write_caches(caches):
  return "["+",".join([write_cache(c) for c in caches])+"]"

def write_cache(cache):
  return "swarming.cache(\"%s\", name=\"%s\")" % (
    cache['path'][0],
    cache['name'][0],
    #cache.get('wait_for_warm_cache_secs', [None])[0],
  )

def write_executable(recipe):
  return """luci.recipe(
    name=\"%s\",
    cipd_package=\"%s\",
    cipd_version=\"%s\",
  )""" % (
    recipe['name'][0],
    recipe['cipd_package'][0],
    recipe['cipd_version'][0],
  )

def process_mixins(cfg):
  mixins = dict()
  for m in cfg['builder_mixins']:
    name = m['name'][0]
    del m['name']
    mixins[name] = dict(m)
  collapse_mixins(mixins)
  return mixins

def collapse_mixins(mixins):
  for name, mix in mixins.items():
    if 'mixins' in mix:
      refs = [mixins[n] for n in mix['mixins']]
      del mix['mixins']
      merge_dicts(mix, refs)

def merge_dict(d_dest, d_src):
  for key, value in d_src.items():
    d_dest.setdefault(key, []).extend(value)

def merge_dicts(d_dest, dict_list):
  for d in dict_list:
    merge_dict(d_dest, d)

def process_builders(cfg):
  builders = dict()
  for bk in cfg['buckets']:
    bucket_name = bk['name'][0]
    defaults = bk['swarming'][0]['builder_defaults'][0]
    for builder in bk['swarming'][0]['builders']:
      name = builder['name'][0]
      del builder['name']
      builders[name] = dict(builder)
      builders[name]['bucket'] = bucket_name
      merge_dict(builders[name], dict(defaults))
  return builders

def collapse_builder_mixins(builders, mixins):
  for builder in builders.values():
    if 'mixins' in builder:
      refs = [mixins[n] for n in builder['mixins']]
      merge_dicts(builder, refs)
      del builder['mixins']

def collapse_builder_recipe_props(builders):
  for builder in builders.values():
    if 'recipe' in builder:
      collapsed = dict()
      merge_dicts(collapsed, builder['recipe'])
      expand_recipe_props(collapsed)
      builder['recipe'] = collapsed
      
def expand_recipe_props(recipe):
  props = dict()
  if 'properties' in recipe:
    props.update(dict({
      str2prop(p) for p in recipe['properties']
    }))
  if 'properties_j' in recipe:
    props.update(dict({
      str2prop(p) for p in recipe['properties_j']
    }))
    del recipe['properties_j']
  recipe['properties']= props

def str2prop(s):
  splited = s.split(':', 1)
  return (splited[0].strip(), splited[1])

if __name__ == '__main__':
  buildbucket2star()