# r2discord — Discord Rich Presence for radare2

Date: 2026-10-12
Status: implemented in v0.1.0

## Goal

A native radare2 core plugin that shows what you are doing in radare2 as
Discord Rich Presence. Ships to the community through `r2pm`.

Non-goals for v1: Windows support, distinguishing graph/panels from plain
visual mode, Discord buttons/party/secrets, any network access other than the
local Discord IPC socket.

## Constraints

- radare2 6.2.x core plugin ABI (`RCorePlugin`, `RLibStruct`), built with
  `pkg-config r_core`. No dependencies beyond libr and libc/pthreads.
- Platforms: Linux and macOS (both use a Unix domain socket for Discord IPC).
- Must never slow down, block, crash or alter radare2. Commands are always
  passed through (`call` returns `false`).
- Private by default: no file or function names leave the machine unless
  the user opts in.
- All user-facing text in English.

## Discord application

- Default application ID: `1559252980589535242` (app named `radare2`).
- Default large image key: `r2` (asset not uploaded yet; presence works
  without it and picks it up once an asset with that key exists).
- Only the application ID is used. No bot, token, client secret or public key.

## Architecture

```
r2 command ──> core_discord.c ──(only on change)──> worker.c ──> ipc.c ──> Discord
                     │                                  ▲
                     └──> presence.c (build text) ──────┘  (mutex-protected latest snapshot)
```

| Unit | Responsibility | Depends on |
|---|---|---|
| `src/ipc.c/.h` | Find socket, connect, frame/unframe messages, handshake, ping/pong, close. Knows nothing about r2. | libc |
| `src/presence.c/.h` | Pure function: `PresenceInput` (mode, format, arch, bits, filename, function, showname, timestamps) → `details`/`state` strings and the `SET_ACTIVITY` JSON. | `r_util` (PJ JSON builder) |
| `src/worker.c/.h` | Background thread. Holds the latest snapshot, coalesces, rate-limits, reconnects with backoff, tracks idle. Never touches `RCore`. | ipc, pthreads |
| `src/core_discord.c` | Plugin entry: registers `e discord.*` vars and the `discord` command; in `call` computes the current mode cheaply and hands a snapshot to the worker on change. | libr, presence, worker |
| `Makefile`, `r2pm` script | Build `.so`/`.dylib`, install to `r2 -H R2_USER_PLUGINS`. | pkg-config |

## Configuration (`e discord.*`)

| Var | Default | Meaning |
|---|---|---|
| `discord.enabled` | `true` | Master switch. Turning it off clears the presence. |
| `discord.showname` | `false` | Include file name and current function. |
| `discord.appid` | `1559252980589535242` | Discord application ID. Changing it reconnects. |
| `discord.image` | `r2` | Large image asset key. |
| `discord.idle` | `300` | Seconds without commands before state becomes `💤 Idle · <stats>`. |

Command `discord` prints connection status and the last sent presence;
`discord?` prints help.

## Mode detection

Verified with a throwaway probe on r2 6.2.0: the core plugin `call` hook
receives every command, including internal ones issued by analysis (`aaa`
produced ~50) and visual redraws; `core->vmode` is `1` inside visual mode.

Evaluated in order on each `call`:

| Mode | Detection | details | state |
|---|---|---|---|
| No file | no opened binary | `radare2` | `📂 No file loaded` |
| Debugging | `cfg.debug` true | `<ARCH>-<BITS> <FMT><stats>` | `🐞 Debugging` |
| Visual | `core->vmode` | same | `👁 Visual mode` |
| Prompt | default | same | `⌨️ Prompt` |
| Idle | worker: no `call` for `discord.idle` s | previous details | `💤 Idle` |

`<stats>` is ` · N fns` when r2 knows any functions (`core->anal->fcns`), plus
` · N bps` while debugging when there are any (`core->dbg->bp->bps`); singular
`fn`/`bp` for 1. Stats sit on the details line because Discord's profile card
shows only ~33 characters per line; the state line stays short. Both counts are
part of the change-detection key. Large image hover text: `radare2 <R2_VERSION>`.
(Revised 2026-10-12 after real use.)

With `discord.showname=true`: details become `<file> · <ARCH>-<BITS> <FMT><stats>`
and in visual mode state becomes `👁 <function>` when the seek is inside a known function.

Start timestamp = time the current file was opened; resets on file change.

Hot path: `call` builds a small fixed-size key (mode, file id, function
address if showname) and compares it to the previous one. Only on change does
it read bin info, build the snapshot and signal the worker. It also stores the
last-activity time (an atomic store) for idle tracking.

## IPC protocol

- Socket search: for each base in `$XDG_RUNTIME_DIR`, `$TMPDIR`, `$TMP`,
  `$TEMP`, `/tmp`; and each prefix `""`, `app/com.discordapp.Discord/`,
  `snap.discord/`; try `discord-ipc-0` … `discord-ipc-9`.
- Frame: `uint32 opcode` (LE), `uint32 length` (LE), JSON payload.
  Opcodes: 0 HANDSHAKE, 1 FRAME, 2 CLOSE, 3 PING, 4 PONG.
- Handshake: `{"v":1,"client_id":"<appid>"}` → expect `DISPATCH`/`READY`.
- Update: op 1 `{"cmd":"SET_ACTIVITY","args":{"pid":<pid>,"activity":{...}},"nonce":"<n>"}`;
  clearing sends `"activity":null`.
- Writes use `MSG_NOSIGNAL` (Linux) / `SO_NOSIGPIPE` (macOS) so a dead socket
  can never raise SIGPIPE in r2.

## Worker behaviour

- Latest-wins snapshot under a mutex + condition variable.
- Minimum 4 s between `SET_ACTIVITY` sends (Discord allows ~5 per 20 s).
- Not connected: retry with backoff 5 s → 10 → 20 → 40 → 60 s cap; silent.
- Write/read failure: drop connection, return to retry loop.
- Handshake rejected (`ERROR` / close with code): stop retrying, record the
  error for `discord`, resume when `discord.appid` changes.
- Shutdown (`fini`): send clear + CLOSE, signal thread, join with a 1 s
  bounded wait, free everything.

## Testing

1. `test/test_presence.c` — text for every mode × showname, article rules,
   arch mapping, JSON escaping of hostile file names (`"`, `\`, newlines, UTF-8).
2. `test/test_ipc.c` — fake Discord server on a Unix socket in a temp dir
   (`XDG_RUNTIME_DIR` pointed at it): handshake, framing, ping→pong, reconnect
   after server restart, rejected handshake.
3. `test/integration.sh` — `r2 -q` with the plugin against the fake server;
   assert a `SET_ACTIVITY` with expected text arrives and `aaa` does not cause
   more than one extra send.
4. Manual check against the real Discord client.
5. GitHub Actions (Linux + macOS) at publication time.

## Distribution

GitHub repo + PR to `radareorg/r2pm-db` so `r2pm -ci r2discord` works.
License MIT. Publication only with the author's explicit go-ahead.
