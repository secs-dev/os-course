#!/usr/bin/env python3
"""Durable HTTP journal for the VTFS kernel module."""

import argparse
import errno
import re
import sqlite3
import struct
from http.server import BaseHTTPRequestHandler, HTTPServer
from urllib.parse import parse_qs, urlsplit

RECORD = struct.Struct("<IIQQQI512s")
STATUS = struct.Struct("<Q")
TOKEN = re.compile(r"[A-Za-z0-9_-]{1,64}")


class Journal:
    def __init__(self, database):
        self.db = sqlite3.connect(database)
        self.db.execute("PRAGMA journal_mode = WAL")
        self.db.execute("PRAGMA synchronous = FULL")
        self.db.execute("""
            CREATE TABLE IF NOT EXISTS journal (
                token TEXT NOT NULL,
                seq INTEGER NOT NULL,
                record BLOB NOT NULL,
                PRIMARY KEY (token, seq)
            )
        """)

    def append(self, token, record):
        with self.db:
            self.db.execute("""
                INSERT INTO journal (token, seq, record)
                SELECT ?, COALESCE(MAX(seq), 0) + 1, ?
                FROM journal WHERE token = ?
            """, (token, record, token))

    def entry(self, token, index):
        row = self.db.execute(
            "SELECT record FROM journal WHERE token = ? AND seq = ?",
            (token, index),
        ).fetchone()
        return row[0] if row else None


class Handler(BaseHTTPRequestHandler):
    def do_POST(self):
        status, payload = self.dispatch()
        body = STATUS.pack(status) + payload
        self.send_response(200)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def dispatch(self):
        path = urlsplit(self.path)
        params = parse_qs(path.query, keep_blank_values=True)
        token, = params.get("token", [""]) if len(params.get("token", [])) == 1 else [""]
        args = params.get("arg", [])
        if not TOKEN.fullmatch(token) or len(args) != 1:
            return errno.EINVAL, b""

        argument = args[0]
        try:
            if path.path == "/api/entry":
                if not argument.isascii() or not argument.isdecimal() or len(argument) > 18:
                    return errno.EINVAL, b""
                record = self.server.journal.entry(token, int(argument))
                return (0, record) if record is not None else (errno.ENOENT, b"")
            if path.path == "/api/append":
                if len(argument) != RECORD.size * 2:
                    return errno.EINVAL, b""
                record = bytes.fromhex(argument)
                action, _, _, _, _, length, _ = RECORD.unpack(record)
                if action not in range(1, 7) or length > 512:
                    return errno.EINVAL, b""
                self.server.journal.append(token, record)
                return 0, b""
        except (ValueError, struct.error):
            return errno.EINVAL, b""
        except sqlite3.Error:
            self.log_error("SQLite operation failed")
            return errno.EIO, b""
        return errno.ENOSYS, b""

    def log_request(self, code="-", size="-"):
        pass


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument("--db", default="vtfs.sqlite3")
    args = parser.parse_args()
    with HTTPServer((args.host, args.port), Handler) as server:
        server.journal = Journal(args.db)
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            pass
        finally:
            server.journal.db.close()


if __name__ == "__main__":
    main()
