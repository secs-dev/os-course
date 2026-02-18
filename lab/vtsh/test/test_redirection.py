import unittest

from base_test import BaseShellTest

REQUIRED_REDIRECTION_FUNCTIONALITY = True

@unittest.skipIf(not REQUIRED_REDIRECTION_FUNCTIONALITY, 
                 ("Redirection functionality is not required in the task. "
                  "This functionality is for an additional task."))
class TestShellRedirection(BaseShellTest):
    def test_simple_redirection(self):
        self.add_test_file("aaa")

        self.execute("echo vt flood > aaa", "")
        self.execute("cat <aaa", "vt flood")
        self.execute("cat >/dev/null < aaa", "")

    def test_redirection_overwrite(self):
        self.add_test_file("aaa")

        self.execute("echo c forever >aaa", "")
        self.execute("cat < aaa", "c forever")

    def test_redirection_with_spaces(self):
        self.add_test_file("aaa")

        self.execute("echo >aaa andrew tanenbaum", "")
        self.execute("<aaa cat", "andrew tanenbaum")

    def test_multiline_redirection(self):
        self.add_test_file("aaa")

        self.execute("> aaa cat\nhello\nworld", "")
        self.execute("< aaa cat", "hello\nworld")

    def test_complex_redirection(self):
        self.add_test_file("aaa")
        self.add_test_file("bbb")

        self.execute(">aaa head -c 250 < /dev/zero", "")
        self.execute("wc > bbb -c aaa", "")
        self.execute("< bbb cat", "250 aaa")

    def test_invalid_redirection_syntax(self):
        self.execute("echo test foo bar>bbb", "test foo bar>bbb")
        self.execute("echo test<aaa>bbb", "test<aaa>bbb")

    def test_combined_redirection(self):
        self.add_test_file("lol")
        self.add_test_file("wut")

        self.execute("echo >lol<wut alpha", "")
        self.execute("cat lol<wut", "alpha")

    def test_syntax_errors(self):
        self.execute("echo >foo > bar", "Syntax error")
        self.execute("cat <one > two <bar", "Syntax error")
        self.execute("wc -l <", "Syntax error")
        self.execute("cat < >hello", "Syntax error")
        self.execute("cat >>hello", "Syntax error")

    def test_io_errors(self):
        self.execute("</dev/zero cat > /sys/proc/foo/bar", "I/O error")
        self.execute("cat < /foo/bar/baz", "I/O error")
        self.execute("cat < baz > foo", "I/O error")
