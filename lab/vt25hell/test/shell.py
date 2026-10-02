import subprocess


class Shell:
    def __init__(self, path: str):
        self._path = path

    def execute(self, cmd: str):
        shell = subprocess.Popen(
            self._path,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            encoding="utf8",
        )

        stdout, _ = shell.communicate(cmd + "\n", timeout=2)
        return shell.returncode, stdout.replace("vtsh> ", "").strip()
