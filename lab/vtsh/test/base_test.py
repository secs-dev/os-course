import os
from typing import Optional
from unittest import TestCase

from shell import Shell


class BaseShellTest(TestCase):
    def setUp(self):
        self.shell = Shell("./build/bin/vtsh")
        self.test_files = set()

    def tearDown(self):
        for file in self.test_files:
            if os.path.exists(file):
                os.remove(file)

    def add_test_file(self, filename: str):
        self.test_files.add(filename)

    def execute(self, cmd: str, expected: Optional[str] = None):
        status, stdout = self.shell.execute(cmd)

        self.assertEqual(status, 0)
        if expected is not None:
            self.assertEqual(stdout, expected)
