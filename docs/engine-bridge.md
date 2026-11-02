# The engine bridge (and Strata)

A running game can be driven from outside while you work on the engine: the launcher's **Mod Studio → Engine**
page uses it, and so can the user's Strata coding AI or any other tool on the same computer.

## Turning it on
- In the game: **Options → Game → Engine bridge**, or start it with `dokuquest --bridge [port]`
  (the Engine page's **Start the game (bridge on)** does that).
- It listens on **127.0.0.1:25590** (this computer only; nothing from the network can reach it).

## Talking to it
One JSON object per line, sent over TCP; the game answers each with one JSON line (`"ok": true|false`,
plus `"error"` when it's false). Put an `"id"` in a request and the answer carries it back.

| Request | Answer |
|---|---|
| `{"cmd":"state"}` | `version`, `fps`, `screen` (the open menu), and with a world open: `world`, `day`, `time`, `player` {x, y, z, yaw, pitch, hp, dim, realm} |
| `{"cmd":"command","text":"/time set noon"}` | `reply`: what the command answered (any chat command, as if typed) |
| `{"cmd":"reloadShaders"}` | `reloaded`: how many shaders built again from disk |
| `{"cmd":"shot","path":"/tmp/a.png"}` | `path`; the picture is the next frame |
| `{"cmd":"log","lines":40}` | `lines`: the last lines of the game's log (`[warn]`, `[error]` marked) |
| `{"cmd":"set","key":"renderDist","value":8}` | `key`; any setting of `settings.json`, saved |

From a shell: `printf '{"cmd":"state"}\n' | nc -q1 127.0.0.1 25590`

## The Engine page
- **Areas**: `src/…`, the shaders, `data/`, `tools/port/`, `docs/`. Pick a file to open it in the editor.
- **Editing**: Save writes the file; saving a `.glsl` with the game connected reloads its shaders at once.
- **Build** runs `cmake --build build --parallel` in the source folder (configure it once first, see the
  README); **Start the game (bridge on)** runs `build/dokuquest --bridge`.
- **The game, live**: its state, a command line, Reload shaders, Screenshot, its log.
- **Ask Strata**: like the Zed editor, the page finds the models running on this computer by itself — it asks
  Ollama (`127.0.0.1:11434/api/tags`), LM Studio (`127.0.0.1:1234/v1/models`) and llama.cpp's server
  (`127.0.0.1:8080/v1/models`) every 20 seconds — so Strata shows up in **Model on this computer** while it runs,
  and is picked by itself the first time (any model with "strata" in its name). The question goes, with the open
  file, to that server's OpenAI-style chat endpoint (`/v1/chat/completions`) through the system `curl`.
  **Elsewhere** takes any other address, model and API-key variable (a hosted model). Code it answers with
  (the first ``` block) can be put into the editor to look over before you Save; nothing is saved by itself.

## As an MCP server (Strata, Claude, any MCP client)
`dokuquest-launcher --mcp [port]` speaks the Model Context Protocol on stdin/stdout and passes each tool call to
the running game's bridge: `game_state`, `run_command`, `reload_shaders`, `screenshot`, `read_log`,
`set_setting`. In an MCP client's settings:
```json
{ "mcpServers": { "dokuquest": { "command": "/path/to/DokuQuest/dokuquest-launcher", "args": ["--mcp"] } } }
```
(the AI edits the engine's files with its own file tools, and uses these to see and test the result live.)
Commands sent through the bridge run with full permissions, even in a world with cheats off: it's the person at
the keyboard.
