#!/usr/bin/env python3
# Copyright 2022 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import sys
import os
# Add depot tools to the sys path, for gerrit_util
sys.path.append(
    os.path.abspath(
        os.path.join(
            os.path.dirname(os.path.abspath(__file__)),
            '../../third_party/depot_tools')))

import logging
import argparse
import base64
import urllib
import re

import gerrit_util

from common_includes import VERSION_FILE

GERRIT_HOST = 'chromium-review.googlesource.com'


def GerritCherryPick(host, change, destination):
  # TODO(leszeks): Add support for cherrypicking to gerrit_util.
  path = 'changes/%s/revisions/current/cherrypick' % (change)
  body = {'destination': destination}
  conn = gerrit_util.CreateHttpConn(host, path, reqtype='POST', body=body)
  return gerrit_util.ReadHttpJsonResponse(conn)


def GerritFileContents(host, change, path):
  # TODO(leszeks): Add support for reading content to gerrit_util.
  path = 'changes/%s/revisions/current/files/%s/content' % (
      change, urllib.parse.quote(path, ''))
  conn = gerrit_util.CreateHttpConn(host, path, reqtype='GET')
  return base64.b64decode(
      gerrit_util.ReadHttpResponse(conn).read()).decode('utf-8')


def GerritChangeEdit(host, change, path, data):
  # TODO(leszeks): Fix ChangeEdit in gerrit_util.
  path = 'changes/%s/edit/%s' % (change, urllib.parse.quote(path, ''))
  body = {
      'binary_content':
          'data:text/plain;base64,%s' %
          base64.b64encode(data.encode('utf-8')).decode('utf-8')
  }
  conn = gerrit_util.CreateHttpConn(host, path, reqtype='PUT', body=body)
  return gerrit_util.ReadHttpResponse(conn, accept_statuses=(204, 409)).read()


def GerritChangeEditMessage(host, change, message):
  # TODO(leszeks): Add support for editing commit messages to gerrit_util.
  path = 'changes/%s/edit:message' % (change)
  body = {'message': message}
  conn = gerrit_util.CreateHttpConn(host, path, reqtype='PUT', body=body)
  return gerrit_util.ReadHttpResponse(conn, accept_statuses=(204, 409)).read()


def ExtractVersion(include_file_text):
  version = {}
  for line in include_file_text.split('\n'):

    def ReadAndPersist(var_name, def_name):
      match = re.match(r"^#define %s\s+(\d*)" % def_name, line)
      if match:
        value = match.group(1)
        version[var_name] = int(value)

    for (var_name, def_name) in [("major", "V8_MAJOR_VERSION"),
                                 ("minor", "V8_MINOR_VERSION"),
                                 ("build", "V8_BUILD_NUMBER"),
                                 ("patch", "V8_PATCH_LEVEL")]:
      ReadAndPersist(var_name, def_name)
  return version


def main():
  logging.basicConfig(level=logging.DEBUG)

  parser = argparse.ArgumentParser(
      description="Use the gerrit API to cherry-pick a revision")
  parser.add_argument(
      "-a",
      "--author",
      default="",
      help="The author email used for code review.")

  group = parser.add_mutually_exclusive_group(required=True)
  group.add_argument("--branch", help="The branch to merge to.")
  # TODO(leszeks): Add support for more than one revision. This will need to
  # cherry pick one of them using the gerrit API, then somehow applying the rest
  # onto it as additional patches.
  parser.add_argument("revision", nargs=1, help="The revision to merge.")

  options = parser.parse_args()

  # Get the original commit.
  revision = options.revision[0]

  # Create a cherry pick commit from the original commit.
  cherry_pick = GerritCherryPick(GERRIT_HOST, revision, options.branch)
  cherry_pick_id = cherry_pick['id']

  # Get the version out of the cherry-picked commit's v8-version.h
  include_file_text = GerritFileContents(GERRIT_HOST, cherry_pick_id,
                                         VERSION_FILE)
  version = ExtractVersion(include_file_text)

  # Increment the patch number in v8-version.h
  new_patch = version['patch'] + 1
  include_file_text = re.sub(
      r"(?<=#define V8_PATCH_LEVEL)(?P<space>\s+)\d*$",
      r"\g<space>%d" % new_patch,
      include_file_text,
      flags=re.MULTILINE)

  # Create the commit message, using the new version and information about the
  # original commit.
  original_commit = gerrit_util.GetChangeCommit(GERRIT_HOST, revision)
  commit_msg = "\n".join([
      "Version %d.%d.%d.%d (cherry-pick)" %
      (version["major"], version["minor"], version["build"], new_patch),  #
      "",  #
      "Merged %s" % original_commit['commit'],  #
      "",  #
      "%s" % original_commit['subject'],  #
  ])

  # Update the v8-version.h contents and the commit message.
  GerritChangeEdit(GERRIT_HOST, cherry_pick_id, VERSION_FILE, include_file_text)
  GerritChangeEditMessage(GERRIT_HOST, cherry_pick_id, commit_msg)
  gerrit_util.PublishChangeEdit(GERRIT_HOST, cherry_pick_id)

  # Set Owners-Override +1
  try:
    gerrit_util.SetReview(
        GERRIT_HOST, cherry_pick_id, labels={"Owners-Override": 1})
  except:
    logging.WARNING("Could not set Owners-Override +1")


if __name__ == "__main__":  # pragma: no cover
  sys.exit(main())
