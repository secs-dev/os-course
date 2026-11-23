#!/bin/sh
# Run from the lab directory with sudo after make. Uses a disposable mount and DB.
set -eu

module=${VTFS_TEST_MODULE:-vtfs}
scratch=$(mktemp -d)
mount_at=$scratch/mnt
mkdir "$mount_at"
server=
loaded=
check="Prepare the test environment"
passed=0

begin_check() {
    check=$1
    printf '[RUN]  %s\n' "$check"
}

pass_check() {
    passed=$((passed + 1))
    printf '[PASS] %s\n' "$check"
}

cleanup() {
    if mountpoint -q "$mount_at"; then umount "$mount_at" || return $?; fi
    if [ -n "$loaded" ]; then rmmod "$module" || return $?; fi
    if [ -n "$server" ]; then
        kill "$server" 2>/dev/null || :
        wait "$server" 2>/dev/null || :
    fi
    rm -rf -- "$scratch"
}
finish() {
    status=$?
    if [ "$status" -ne 0 ]; then
        printf '[FAIL] %s (exit %s)\n' "$check" "$status" >&2
    fi
    cleanup_status=0
    cleanup || cleanup_status=$?
    if [ "$cleanup_status" -ne 0 ]; then
        printf '[FAIL] Clean up the test environment (exit %s)\n' "$cleanup_status" >&2
        if [ "$status" -eq 0 ]; then status=$cleanup_status; fi
    fi
    if [ "$status" -eq 0 ]; then
        printf 'VTFS: all %s checks passed.\n' "$passed"
    fi
    exit "$status"
}
trap finish EXIT
begin_check "$check"
if [ -d "/sys/module/$module" ]; then
    echo "$module is already loaded. Use a clean VM or a renamed test build." >&2
    exit 1
fi

port=18080
start_server() {
    python3 server.py --port "$port" --db "$scratch/journal.sqlite3" &
    server=$!
    python3 - "$port" <<'PY'
import socket
import sys
import time
for attempt in range(50):
    try:
        with socket.create_connection(("127.0.0.1", int(sys.argv[1])), 0.5):
            break
    except OSError:
        time.sleep(0.1)
else:
    raise SystemExit("VTFS HTTP server did not start")
PY
    kill -0 "$server"
}

start_server

insmod "./$module.ko" server_port="$port"
loaded=1
pass_check

begin_check "RAM mount and root inode"
mount -t "$module" TODO "$mount_at"
test "$(stat -c '%a:%i' "$mount_at")" = 777:1000
pass_check

begin_check "RAM file creation, listing, and deletion"
mkdir "$mount_at/dir"
touch "$mount_at/new_file.txt"
ls -1 "$mount_at" | grep -Fqx new_file.txt
rm "$mount_at/new_file.txt"
test ! -e "$mount_at/new_file.txt"
pass_check

begin_check "RAM binary data, hard links, and directory removal"
printf 'hello\000world' >"$mount_at/dir/test.txt"
ln "$mount_at/dir/test.txt" "$mount_at/link"
test "$(stat -c %i "$mount_at/link")" = "$(stat -c %i "$mount_at/dir/test.txt")"
rm "$mount_at/dir/test.txt"
printf 'hello\000world' | cmp - "$mount_at/link"
rmdir "$mount_at/dir"
pass_check

begin_check "RAM contents cleared after remount"
umount "$mount_at"
mount -t "$module" TODO "$mount_at"
test ! -e "$mount_at/link"
umount "$mount_at"
pass_check

begin_check "HTTP file and directory operations"
mount -t "$module" remote:smoke "$mount_at"
mkdir "$mount_at/dir"
printf 'abc\000def' >"$mount_at/dir/data"
ln "$mount_at/dir/data" "$mount_at/link"
truncate -s 4 "$mount_at/link"
printf 'ghi' >>"$mount_at/link"
rm "$mount_at/dir/data"
touch "$mount_at/dir/deleted"
rm "$mount_at/dir/deleted"
mkdir "$mount_at/deleted-dir"
rmdir "$mount_at/deleted-dir"
pass_check

begin_check "HTTP outage: write refused and contents preserved"
# Keep the same mount and inode across the server outage.
kill "$server"
wait "$server" 2>/dev/null || :
server=
timeout -k 2s 8s python3 - "$mount_at/link" <<'PY'
import errno
import os
import sys

with open(sys.argv[1], "ab", buffering=0) as file:
    try:
        os.write(file.fileno(), b"lost")
    except OSError as error:
        if error.errno != errno.ECONNREFUSED:
            raise
    else:
        raise SystemExit("VTFS write unexpectedly succeeded with the server stopped")
PY
printf 'abc\000ghi' | timeout -k 2s 8s cmp - "$mount_at/link"
pass_check

begin_check "HTTP restart: write on the same mount"
start_server
timeout -k 2s 8s sh -c 'printf "%s" recovered >>"$1"' sh "$mount_at/link"
printf 'abc\000ghirecovered' | timeout -k 2s 8s cmp - "$mount_at/link"
pass_check

begin_check "SQLite replay after remount"
umount "$mount_at"
mount -t "$module" remote:smoke "$mount_at"
printf 'abc\000ghirecovered' | cmp - "$mount_at/link"
test -d "$mount_at/dir"
test ! -e "$mount_at/dir/data"
test ! -e "$mount_at/dir/deleted"
test ! -e "$mount_at/deleted-dir"
umount "$mount_at"
pass_check
