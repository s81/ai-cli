# ai-cli

A cross-platform terminal session manager for [Claude Code](https://claude.ai/code). Run multiple Claude sessions side by side, switch between them instantly, and keep your work organized.

```
 ai-cli  v0.1.0
╭──────────────────────╮╭──────────────────────────────────────────────╮
│ Sessions             ││                                              │
├──────────────────────┤│  $ claude                                    │
│ ▶ backend-api        ││  > help me refactor this function            │
│   frontend-ui        ││  ...                                         │
│   infra              ││                                              │
├──────────────────────┤│                                              │
│  [+ New]             ││                                              │
╰──────────────────────╯╰──────────────────────────────────────────────╯
 ^N new  ^W kill  /: search  Tab: next  Enter: focus  ?: help  ^Q quit
```

## Installation

Download the binary for your platform from the [latest release](https://github.com/s81/ai-cli/releases/latest):

| Platform | File |
|---|---|
| Linux (x86_64) | `ai-cli-linux-x86_64` |
| macOS (Universal) | `ai-cli-macos-universal` |
| Windows (x86_64) | `ai-cli-windows-x86_64.exe` |

**Linux / macOS:**
```bash
curl -L https://github.com/s81/ai-cli/releases/latest/download/ai-cli-linux-x86_64 -o ai-cli
chmod +x ai-cli
./ai-cli
```

**Windows:** Download `ai-cli-windows-x86_64.exe` and run it from a terminal.

> **Requirement:** [Claude Code](https://claude.ai/code) must be installed and the `claude` command must be on your `PATH`.

## Usage

Launch the app:
```bash
./ai-cli
```

The screen is split into two panes:

- **Left sidebar** — your session list. Dim entries are sessions that have exited.
- **Right pane** — the active session's terminal output.

### Keyboard shortcuts

| Key | Action |
|---|---|
| `Ctrl+N` | Create a new session |
| `Tab` | Select next session |
| `Shift+Tab` | Select previous session |
| `Enter` | Focus the terminal pane (start typing to Claude) |
| `Ctrl+B` | Return focus to the sidebar |
| `Ctrl+W` | Kill the active session |
| `/` | Search / filter sessions |
| `Escape` | Exit search |
| `?` or `F1` | Toggle keyboard reference |
| `Ctrl+Q` | Quit |

### Creating a session

Press `Ctrl+N`, type a name (e.g. `backend-api`), and press `Enter`. A new Claude session starts immediately. Leave the name blank to get an auto-generated one.

### Switching sessions

Use `Tab` / `Shift+Tab` to cycle through the list, or press `/` and type part of a session name to filter.

### Talking to Claude

Select a session and press `Enter` to focus the terminal pane. Type normally — keystrokes go directly to Claude. Press `Ctrl+B` to return to the sidebar at any time.

### Session persistence

Sessions are saved to `~/.ai-cli/sessions.json` (Linux/macOS) or `%APPDATA%\ai-cli\sessions.json` (Windows) so they survive restarts.

## Building from source

**Prerequisites:** CMake 3.21+, a C++17 compiler, Git.

```bash
git clone --recurse-submodules https://github.com/s81/ai-cli.git
cd ai-cli
./vcpkg/bootstrap-vcpkg.sh   # bootstrap-vcpkg.bat on Windows
cmake -B build -DCMAKE_TOOLCHAIN_FILE=vcpkg/scripts/buildsystems/vcpkg.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Binary is at `build/ai-cli` (or `build\Release\ai-cli.exe` on Windows).

**Run tests:**
```bash
ctest --test-dir build --output-on-failure
```

## License

MIT
