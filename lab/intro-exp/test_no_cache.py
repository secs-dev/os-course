"""Run with python3 lab/intro-exp/test_no_cache.py on Linux or macOS.

Linux requires a filesystem supporting O_DIRECT in the temporary directory.
Set TMPDIR to such a filesystem if necessary; unsupported I/O fails the test.
"""
import os
from pathlib import Path
import random
import shlex
import struct
import subprocess
import tempfile
import unittest


class NoCacheTest(unittest.TestCase):
    def test_traversal(self):
        source = Path(__file__).resolve().parent / "src"
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            for name in ("graph_traverse", "graph_traverse_mmap"):
                executable = directory / name
                subprocess.run(
                    shlex.split(os.environ.get("CC", "cc"))
                    + ["-Wall", "-Wextra", "-Werror", "-O2",
                       str(source / (name + ".c")), "-o", str(executable)],
                    check=True,
                )
                # Small EOF, block-spanning records, multiple blocks, and
                # an exactly 4096-byte file (40 + 169 * 24).
                for count in (1, 169, 200, 513):
                    order = list(range(count))
                    random.Random(427).shuffle(order)
                    original = bytearray(struct.pack(
                        "<8sIQIIQI", b"GCACHEG1", 1, count, 24, 1, order[0], 0
                    ))
                    for index in range(count):
                        original.extend(struct.pack("<qIIQ", index, 0, 0xA5A5A5A5, 0))
                    for position, index in enumerate(order[:-1]):
                        struct.pack_into("<I", original, 40 + index * 24 + 8, 1)
                        struct.pack_into("<Q", original, 40 + index * 24 + 16,
                                         order[position + 1])
                    for no_cache in (False, True):
                        for writing in (False, True):
                            with self.subTest(program=name, count=count,
                                              no_cache=no_cache, writing=writing):
                                paths = [directory / "first.bin", directory / "second.bin"]
                                for path in paths:
                                    path.write_bytes(original)
                                command = [str(executable)]
                                if no_cache:
                                    command.append("--no-cache")
                                if writing:
                                    command.append("--write")
                                result = subprocess.run(
                                    command + ["3"] + list(map(str, paths)),
                                    capture_output=True, text=True, timeout=60,
                                )
                                self.assertEqual(result.returncode, 0, result.stderr)
                                self.assertEqual(result.stderr.count(
                                    f"OK ({count} nodes processed)"), 3)
                                for path, visits in zip(paths, (2, 1)):
                                    expected = original.copy()
                                    if writing:
                                        for index in range(count):
                                            struct.pack_into("<q", expected, 40 + index * 24,
                                                             index + visits)
                                    self.assertEqual(path.read_bytes(), expected)


if __name__ == "__main__":
    unittest.main()
