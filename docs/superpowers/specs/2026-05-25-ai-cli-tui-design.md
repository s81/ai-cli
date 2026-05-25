# ai-cli: Cross-Platform TUI Session Manager for Claude Code

**Date:** 2026-05-25
**Status:** Approved

---

## Overview

`ai-cli` is a cross-platform TUI application written in C++17 that acts as a session manager and launcher for Claude Code. It embeds real PTY sessions inside a terminal UI, letting users create, name, switch between, search, and persist multiple Claude Code sessions — similar to tmux but purpose-built for Claude Code.

---

## Architecture

### Approach

Single-process TUI with embedded PTY multiplexer. One binary owns the FTXUI render loop, PTY lifecycle management, and I/O multiplexing. No daemon, no IPC.

**Trade-off accepted:** If the process crashes, all sessions die. This matches Ghostty's model and is acceptable for v1.

### Component Map

```
ai-cli/
├── src/
│   ├── main.cpp                — entry point, constructs App and runs event loop
│   ├── app.cpp / app.h         — top-level App; owns Session vector, wires UI + PTY I/O
│   ├── session.cpp / session.h — Session struct + PTY handle + output ring buffer
│   ├── session_store.cpp / session_store.h  — load/save sessions.json
│   ├── pty/
│   │   ├── pty.h               — abstract Pty interface (create, write, resize, kill, read)
│   │   ├── pty_unix.cpp        — forkpty implementation (Linux + macOS)
│   │   └── pty_win.cpp         — ConPTY implementation (Windows)
│   └── ui/
│       ├── session_list.cpp / session_list.h  — left sidebar: list, search, keyboard nav
│       └── terminal_view.cpp / terminal_view.h — right pane: PTY output renderer
├── docs/
├── CMakeLists.txt
└── vcpkg.json
```

### Data Flow

1. `App` owns a `std::vector<Session>`.
2. Each `Session` holds a `Pty` handle and an output ring buffer (`std::deque<std::string>`).
3. A background `std::thread` per session reads PTY stdout and appends lines to the ring buffer.
4. After each append, `ScreenInteractive::PostEvent` signals FTXUI to re-render.
5. The render loop draws `SessionList` (left, fixed width) + `TerminalView` (right, fills remaining space).
6. User keypresses are dispatched: sidebar keys go to `SessionList`; all other keys are forwarded raw to the active session's PTY stdin.

---

## Portability

Portability is a first-class constraint. Platform-specific code is strictly isolated.

### PTY Abstraction

```cpp
// pty/pty.h — the only PTY type the rest of the codebase sees
class Pty {
public:
    virtual ~Pty() = default;
    virtual bool spawn(const std::string& cmd, const std::string& dir) = 0;
    virtual void write(std::string_view data) = 0;
    virtual void resize(int cols, int rows) = 0;
    virtual void kill() = 0;
    virtual int  pid() const = 0;
    virtual bool running() const = 0;
};

std::unique_ptr<Pty> make_pty();  // factory — returns platform impl
```

`#ifdef _WIN32` appears only inside `pty_win.cpp`. `pty_unix.cpp` uses `forkpty` + `execvp`.

### Other Portability Points

| Concern | Solution |
|---|---|
| File paths | `std::filesystem::path` + `config_dir()` helper returning `~/.ai-cli` (Unix) or `%APPDATA%\ai-cli` (Windows) |
| Threading | `std::thread` + `std::atomic<bool>` — C++17 standard |
| Session signals | Abstracted behind `Pty::resize()` and `Pty::kill()` |
| Line endings | Ring buffer normalizes `\r\n` → `\n` on Windows |
| C++ standard | C++17 throughout (`std::filesystem`, `std::optional`, `std::string_view`) |

---

## UI Layout

### Screen Structure (Layout A — fixed sidebar)

```
┌──────────────────────────────────────────────────────────────────┐
│ ai-cli                                               v0.1.0      │
├──────────────────┬───────────────────────────────────────────────┤
│ Sessions         │                                               │
│ ────────────     │   [Active PTY output fills this pane]        │
│ ▶ my-project     │                                               │
│   api-rewrite    │   claude-code running...                     │
│   hotfix-auth    │   > fix the auth bug                         │
│   scratch        │   Sure! Looking at auth.cpp...               │
│                  │                                               │
│                  │                                               │
│ [+ New]          │                                               │
├──────────────────┴───────────────────────────────────────────────┤
│ ^N new  ^W kill  / search  Tab switch  ^Q quit                   │
└──────────────────────────────────────────────────────────────────┘
```

- Sidebar: fixed 20 columns wide, scrollable session list
- Terminal pane: fills remaining width, renders ring buffer lines
- Status bar: single line, always visible, shows active keybindings
- Header: app name + version

### Search Mode

Pressing `/` replaces the sidebar header with an inline text input. The session list filters live as the user types. `Escape` clears the filter and exits search mode.

```
│ / api█           │
│ ────────────     │
│   api-rewrite    │
```

### New Session Dialog

`Ctrl+N` opens a small centered modal with two fields: session name and working directory (pre-filled with `$PWD`). `Enter` confirms, `Escape` cancels.

---

## Keyboard Map

| Key | Scope | Action |
|---|---|---|
| `Ctrl+N` | Global | Open new session dialog |
| `Ctrl+W` | Global | Kill active session (confirm if running) |
| `Tab` | Sidebar focused | Move selection down |
| `Shift+Tab` | Sidebar focused | Move selection up |
| `Enter` | Sidebar focused | Switch to selected session, focus PTY pane |
| `Ctrl+Tab` | Global | Cycle to next session |
| `/` | Sidebar focused | Enter search/filter mode |
| `Escape` | Search mode | Exit search, restore full list |
| `Ctrl+B` | PTY pane focused | Return focus to sidebar (prefix key, like tmux) |
| `Ctrl+Q` | Global | Quit (prompts if sessions are running) |
| `F1` / `?` | Global | Toggle help overlay |

All other keypresses while the PTY pane is focused are forwarded raw to the active PTY stdin.

---

## Session Lifecycle

```
[New dialog confirmed]
        │
        ▼
  Pty::spawn() ──── fails ──▶ show error in status bar
        │
        ▼
   [Running] ◀──── startup: PID still alive → Running; PID dead → Dead
        │
  user kills (^W) ──────────▶ Pty::kill() ──▶ [Dead]
  process exits naturally ──────────────────▶ [Dead]
        │
   [Dead] — shown greyed out in sidebar, removable with Delete key
```

On startup, `SessionStore::load()` reads `sessions.json`. For each session with a saved PID, the app checks if that PID is still alive (via `kill(pid, 0)` on Unix, `OpenProcess` on Windows). Live PIDs transition to `Running`; dead ones become `Dead`.

---

## Data Model & Persistence

### Session struct

```cpp
struct Session {
    std::string              id;          // UUID v4
    std::string              name;
    std::filesystem::path    dir;
    std::string              command;     // default: "claude"
    std::unique_ptr<Pty>     pty;
    std::deque<std::string>  ring_buffer; // last 1000 lines
    SessionState             state;       // Running | Dead
    std::string              created_at;  // ISO 8601
    std::string              last_active; // ISO 8601
};
```

### Persistence file

Location: `~/.ai-cli/sessions.json` (Unix) / `%APPDATA%\ai-cli\sessions.json` (Windows)

```json
{
  "sessions": [
    {
      "id": "550e8400-e29b-41d4-a716-446655440000",
      "name": "my-project",
      "dir": "/home/s81/projects/foo",
      "command": "claude",
      "pid": 12345,
      "created_at": "2026-05-25T23:39:00Z",
      "last_active": "2026-05-25T23:55:00Z"
    }
  ]
}
```

The ring buffer is in-memory only — not persisted. PTY scrollback on re-attach is whatever the claude process has buffered in the kernel.

`SessionStore` writes atomically: serialize to a temp file, then rename over the target. This prevents corruption on crash.

---

## Build System

### Dependencies (`vcpkg.json`)

```json
{
  "name": "ai-cli",
  "version": "0.1.0",
  "dependencies": [
    "ftxui",
    "nlohmann-json"
  ]
}
```

### CMake (`CMakeLists.txt`)

```cmake
cmake_minimum_required(VERSION 3.21)
project(ai-cli VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(ftxui CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

set(SOURCES
    src/main.cpp
    src/app.cpp
    src/session.cpp
    src/session_store.cpp
    src/ui/session_list.cpp
    src/ui/terminal_view.cpp
)

if(WIN32)
    list(APPEND SOURCES src/pty/pty_win.cpp)
else()
    list(APPEND SOURCES src/pty/pty_unix.cpp)
endif()

add_executable(ai-cli ${SOURCES})

target_link_libraries(ai-cli PRIVATE
    ftxui::screen
    ftxui::dom
    ftxui::component
    nlohmann_json::nlohmann_json
)

if(WIN32)
    target_link_libraries(ai-cli PRIVATE kernel32)
endif()

if(APPLE)
    target_compile_definitions(ai-cli PRIVATE _DARWIN_C_SOURCE)
endif()

install(TARGETS ai-cli RUNTIME DESTINATION bin)
```

vcpkg is vendored as a git submodule. Contributors need only CMake 3.21+ and a C++ compiler.

### Developer Workflow

```bash
# Clone
git clone --recurse-submodules <repo>

# Configure + build
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build

# Run
./build/ai-cli          # Linux/macOS
build\Debug\ai-cli.exe  # Windows
```

### CI Matrix (GitHub Actions)

| Runner | Compiler | Notes |
|---|---|---|
| `ubuntu-latest` | GCC 12 | Primary dev target |
| `macos-latest` | Clang (Xcode) | ARM + x86_64 |
| `windows-latest` | MSVC 2022 | ConPTY path |

Each job: configure → build → smoke test (launch, create session, quit cleanly).

---

## Error Handling

- PTY spawn failure: display error message in status bar, session stays in `Dead` state
- `sessions.json` parse error on startup: log warning, start with empty session list (do not crash)
- Atomic write failure for `sessions.json`: log to stderr, continue (in-memory state is authoritative)
- PTY read thread error: mark session `Dead`, notify render thread

---

## Out of Scope (v1)

- Session sharing / remote attach
- Split panes (multiple PTYs visible simultaneously)
- Themes / color customization
- Plugin system
- Session output search
- Export / logging PTY output to file
