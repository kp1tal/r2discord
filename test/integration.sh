#!/bin/sh
# r2discord - MIT - Copyright 2026 - kp1tal
# End to end: r2 + plugin + fake Discord. Needs `make integration`.
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
ext=${EXT:-so}
tmp=$(mktemp -d)
fpid=
cleanup() {
	[ -n "$fpid" ] && kill "$fpid" 2>/dev/null || true
	rm -rf "$tmp"
}
trap cleanup EXIT
sock="$tmp/discord-ipc-0"
log="$tmp/frames.log"
cp /bin/ls "$tmp/r2d_secret_bin"
"$here/build/fake_discord" "$sock" >"$log" &
fpid=$!
i=0
while [ ! -S "$sock" ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i + 1)); done

fail() {
	echo "FAIL: $1"
	echo "--- frames"; cat "$log"
	echo "--- r2"; cat "$tmp/r2.out"
	exit 1
}

if ! R2DISCORD_IPC_PATH="$sock" r2 -q -NN -e scr.interactive=true \
	-c "L $here/build/core_discord.$ext; aaa; discord; !sleep 1" "$tmp/r2d_secret_bin" >"$tmp/r2.out" 2>&1; then
	fail "r2 exited with an error"
fi

sleep 0.5
kill "$fpid"
wait "$fpid" 2>/dev/null || true
fpid=

grep -q '^0 .*"client_id":"1559252980589535242"' "$log" || fail "no handshake"
grep -Eq '^1 .*"details":"Analyzing an? [^"]+ binary"' "$log" || fail "no presence"
grep -q '"state":"At the prompt"' "$log" || fail "wrong state"
if grep -q 'r2d_secret_bin' "$log"; then fail "file name leaked"; fi
n=$(grep -c '^1 .*"details"' "$log")
[ "$n" -le 2 ] || fail "too many updates: $n"
grep -q '^1 .*"activity":null' "$log" || fail "no clear on exit"
grep -q '^2 ' "$log" || fail "no CLOSE on exit"
grep -q 'status: ' "$tmp/r2.out" || fail "discord command printed nothing"
echo "integration: OK ($n presence update(s))"

# Non interactive sessions (r2 -qc, r2pipe) must not touch Discord.
log="$tmp/frames2.log"
"$here/build/fake_discord" "$sock" >"$log" &
fpid=$!
i=0
while [ ! -S "$sock" ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i + 1)); done
if ! R2DISCORD_IPC_PATH="$sock" r2 -q -NN \
	-c "L $here/build/core_discord.$ext; aaa; discord; !sleep 1" "$tmp/r2d_secret_bin" >"$tmp/r2.out" 2>&1; then
	fail "r2 (non interactive) exited with an error"
fi
sleep 0.5
kill "$fpid"
wait "$fpid" 2>/dev/null || true
fpid=
if grep -q '^0 ' "$log"; then fail "non interactive session handshook"; fi
echo "integration: OK (non interactive: no handshake)"
