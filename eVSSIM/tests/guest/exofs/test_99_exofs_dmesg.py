#!/usr/bin/env python
"""
Runs last: the whole exofs suite must not have produced kernel BUGs,
WARNINGs or oopses. Expected error messages (e.g. "EXOFS: error ..." from
the negative mount test) are not considered problems.
"""

from __future__ import print_function

import unittest

import exofs_helpers as h


class TestKernelLogIsClean(unittest.TestCase):

    def test_no_kernel_bugs_or_warnings(self):
        _, log = h.run(["dmesg"], quiet=True)
        problems = h.dmesg_problems(log)
        self.assertEqual(problems, [], "kernel log contains problems:\n%s" % "\n".join(problems))

    def test_exofs_still_unmountable_and_removable(self):
        h.tear_down()
        self.assertFalse(h.is_mounted())
        if h.module_loaded():
            h.rmmod()
        self.assertFalse(h.module_loaded())
