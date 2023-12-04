# Copyright 2023 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from requests.exceptions import HTTPError

from blinkpy.w3c.test_exporter import TestExporter
from blinkpy.w3c.wpt_github import GitHubError

class V8TestExporter(TestExporter):

    def merge_pull_request(self, pull_request):
        self.approve(pull_request)
        super().merge_pull_request(pull_request)


    def approve(self, pr_number):
        """Approves a PR.
        """
        path = '/repos/%s/%s/pulls/%d/reviews'  % (
            self.github.gh_org,
            self.github.gh_repo_name,
            pr_number)

        body = {
            'body': ('The review process for this patch is being conducted in '
                'the V8 project.'),
            'event': 'APPROVE',
            'comments': [],
        }

        response = self.request(path, method='POST', body=body)

        if response.status_code != 200:
            raise GitHubError(200, response.status_code,
                              'ap[rove PR %d' % pr_number)
