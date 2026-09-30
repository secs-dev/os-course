import os
from pathlib import Path
from typing import Optional
from unittest import TestCase

from shell import Shell

PROJECT_ROOT = Path(__file__).resolve().parent.parent
VTSH_EXECUTABLE = PROJECT_ROOT / "build" / "bin" / "vtsh"


class BaseShellTest(TestCase):
    def setUp(self):
        self.shell = Shell(str(VTSH_EXECUTABLE))
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
