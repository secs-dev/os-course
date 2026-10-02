import os

from base_test import BaseShellTest


class TestShellBasics(BaseShellTest):
    def test_empty_command(self):
        self.execute("", "")

    def test_simple_echo(self):
        self.execute("echo hello", "hello")
        self.execute(" echo    hello  world", "hello world")

    def test_multiple_commands(self):
        self.execute("echo hello\necho world", "hello\nworld")

    def test_file_operations(self):
        self.add_test_file("./foo")

        self.execute("touch ./foo", "")
        self.assertTrue(os.path.exists("./foo"))
        self.execute("rm ./foo", "")
        self.assertFalse(os.path.exists("./foo"))

    def test_multiple_file_operations(self):
        self.add_test_file("./bar")

        self.execute("touch ./bar\ncat ./bar\nwc -c bar\nrm ./bar", "0 bar")
        self.assertFalse(os.path.exists("./bar"))

    def test_stdin_interaction(self):
        self.execute("cat\nhello\nworld", "hello\nworld")

    def test_nested_shells(self):
        self.execute("./shell\necho hi\n./shell\necho hello", "hi\nhello")

    def test_invalid_paths(self):
        self.execute("cat /sys/proc/foo/bar", "")
        self.execute("foobar", "Command not found")

