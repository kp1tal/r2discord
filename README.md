# r2discord

Discord Rich Presence for [radare2](https://github.com/radareorg/radare2).

Shows what you are doing in r2 on your Discord profile:

> **radare2**
> Analyzing an ELF x86-64 binary
> In visual mode · 00:12:34 elapsed

It is private by default: file and function names are only shown after
`e discord.showname=true`.

## Install

Requirements: radare2 ≥ 6.2 with its development headers, a C compiler,
`make`, `pkg-config`. Linux or macOS.

```sh
make
make install      # copies the plugin to $(r2 -H R2_USER_PLUGINS)
```

Restart r2. The presence starts after the first command you run; `discord`
then shows the connection status.

## Configuration

| Variable | Default | Meaning |
|---|---|---|
| `discord.enabled` | `true` | Turn the presence on/off |
| `discord.showname` | `false` | Show file and function names |
| `discord.appid` | `1559252980589535242` | Discord application id |
| `discord.image` | `r2` | Large image asset key |
| `discord.idle` | `300` | Seconds without commands before showing `Idle` (0 = never) |

Put your choices in `~/.radare2rc`, e.g. `e discord.showname=true`.

## What it shows

| You are | Details | State |
|---|---|---|
| r2 with no file | radare2 | No file loaded |
| at the prompt | Analyzing an ELF x86-64 binary | At the prompt |
| in visual/graph/panels | Analyzing an ELF x86-64 binary | In visual mode |
| debugging | Debugging an ELF x86-64 binary | Debugger attached |
| away for `discord.idle` s | (unchanged) | Idle |

With `discord.showname=true`, details become `Analyzing <file> (ELF x86-64)`
and in visual mode the state becomes `In <function>`.

The presence follows the state radare2 was in when you typed your last command, so a mode change shows up on the next command.

## How it works

The plugin talks to the local Discord client over its IPC Unix socket
(`discord-ipc-N` under `$XDG_RUNTIME_DIR`, `$TMPDIR` or `/tmp`, including the
Flatpak and Snap locations). The plugin itself sends nothing over the network; Discord shows the presence
to your friends.
A background thread sends at most one update every 4 seconds and reconnects
when Discord restarts. r2 is never blocked waiting for Discord while you work; quitting waits at most
about a second if Discord is not responding.

The presence is only published in interactive sessions: scripts, `r2 -qc` and
r2pipe do not touch Discord.

Set `R2DISCORD_IPC_PATH` to force a specific socket path.

## Development

```sh
make test          # unit tests against a fake Discord server
make integration   # r2 + plugin + fake Discord
```

## License

MIT
