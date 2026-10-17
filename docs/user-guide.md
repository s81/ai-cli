# ai-cli User Guide

ai-cli is a terminal app for running several [Claude Code](https://claude.ai/code) sessions at once. Each session is a separate `claude` process with its own working directory. A sidebar lets you switch between them without juggling terminal tabs.

This guide covers installation, daily use, every keyboard shortcut, where data is stored, and what to do when something goes wrong.

---

## Contents

1. [Requirements](#1-requirements)
2. [Installation](#2-installation)
3. [Your first session](#3-your-first-session)
4. [The screen](#4-the-screen)
5. [Focus: sidebar vs. terminal pane](#5-focus-sidebar-vs-terminal-pane)
6. [Working with sessions](#6-working-with-sessions)
7. [Keyboard reference](#7-keyboard-reference)
8. [Saved sessions and data files](#8-saved-sessions-and-data-files)
9. [Known limitations](#9-known-limitations)
10. [Troubleshooting](#10-troubleshooting)
11. [Uninstalling](#11-uninstalling)
12. [Building from source](#12-building-from-source)

---

## 1. Requirements

| Requirement | Details |
|---|---|
| Claude Code | The `claude` command must be installed, signed in, and on your `PATH`. Check this by running `claude` in a normal terminal first. |
| Terminal | Any modern terminal that supports full-screen apps: Windows Terminal, iTerm2, macOS Terminal, GNOME Terminal, Alacritty, kitty, and so on. Use a window at least 80 columns wide. |
| OS | Linux x86_64, macOS (Apple Silicon), or Windows 10 1809+ / Windows 11 (ai-cli uses the Windows ConPTY API). |

> **Windows note:** ai-cli starts `claude` directly, not through a shell. For security, it only looks for `claude.exe` in the **absolute** folders on your `PATH`, never in the current directory. If you installed Claude Code through npm, `claude` is a `.cmd` shim and ai-cli can't start it. See [Troubleshooting](#a-new-session-is-dim-immediately--shows-no-output).

---

## 2. Installation

Download the binary for your platform from the [latest release](https://github.com/s81/ai-cli/releases/latest).

| Platform | Release file |
|---|---|
| Linux (x86_64) | `ai-cli-linux-x86_64` |
| macOS | `ai-cli-macos-universal` |
| Windows (x86_64) | `ai-cli-windows-x86_64.exe` |

### Linux

```bash
curl -L https://github.com/s81/ai-cli/releases/latest/download/ai-cli-linux-x86_64 -o ai-cli
```

```bash
chmod +x ai-cli && sudo mv ai-cli /usr/local/bin/
```

### macOS

```bash
curl -L https://github.com/s81/ai-cli/releases/latest/download/ai-cli-macos-universal -o ai-cli
```

```bash
chmod +x ai-cli && xattr -d com.apple.quarantine ai-cli && sudo mv ai-cli /usr/local/bin/
```

The binary isn't signed. The `xattr` step stops Gatekeeper from blocking it. Despite its name, the macOS file is built on GitHub's Apple Silicon runners and targets arm64.

### Windows

1. Download `ai-cli-windows-x86_64.exe`.
2. Rename it to `ai-cli.exe` and move it to a folder on your `PATH`, for example `%USERPROFILE%\bin`.
3. Run it from Windows Terminal or PowerShell. Double-clicking it works too, but it opens in the older console host.

### Verify the download (recommended)

Releases from the next version onward include a `SHA256SUMS.txt` file. Download it next to the binary and check that they match:

```bash
sha256sum --check --ignore-missing SHA256SUMS.txt
```

On macOS use `shasum -a 256 --check --ignore-missing SHA256SUMS.txt`. On Windows, run `Get-FileHash <file> -Algorithm SHA256` in PowerShell and compare the result. See [SECURITY.md](../SECURITY.md#verifying-a-download).

### Check the install

```bash
ai-cli
```

You should see the ai-cli header, an empty sidebar, and the message *"No sessions. Press Ctrl+N to create one."* Press `Ctrl+Q` to quit.

---

## 3. Your first session

1. Start ai-cli from any directory.
2. Press **`Ctrl+N`**. The **New Session** dialog opens.
3. Type a name, for example `backend-api`. Press **`Tab`** to move to the directory field. You can type a path such as `~/code/backend`, or leave it blank to use the directory you launched ai-cli from.
4. Press **`Enter`**. Claude Code starts in that directory and the session appears in the sidebar, selected.
5. Press **`Enter`** again to put the **terminal pane** in focus. Your keystrokes now go to Claude.
6. When you want to switch sessions, press **`Ctrl+B`** to move focus back to the sidebar.

That's the whole workflow: `Ctrl+N` to create, `Enter` to talk, `Ctrl+B` to step back, `Tab` to switch.

---

## 4. The screen

```
 ai-cli  v0.1.0                                         ← header
╭──────────────────────╮╭──────────────────────────────╮
│ Sessions             ││                              │
├──────────────────────┤│  Output of the selected      │
│ ▶ backend-api        ││  session. Follows the newest │
│   frontend-ui        ││  output automatically.       │
│   infra              ││                              │
├──────────────────────┤│                              │
│  [+ New]             ││                              │
╰──────────────────────╯╰──────────────────────────────╯
 ^N new  ^W kill  /: search  Tab: next  Enter: focus  ?: help  ^Q quit
                                                        ← status bar
```

| Area | What it shows |
|---|---|
| **Header** | App name and version. |
| **Sidebar** | All sessions. `▶` and inverted colors mark the selected one. **Dim** names are sessions whose process has stopped. |
| **Terminal pane** | Output of the selected session. It always shows the most recent lines, and up to 1,000 lines are kept per session. |
| **Status bar** | The shortcuts that work right now. It changes depending on focus. |

---

## 5. Focus: sidebar vs. terminal pane

This is the main idea to understand. Focus is always on **one of two places**, and keys behave differently in each.

### Sidebar focused (default)

Keys control ai-cli: create, switch, search, kill, help, quit. Letters you type here do nothing unless search is open, so stray keys can't change anything by accident.

### Terminal pane focused (after pressing `Enter`)

Keys go to Claude, with **only two exceptions**:

| Key | Handled by ai-cli |
|---|---|
| `Ctrl+B` | Return focus to the sidebar |
| `Ctrl+Q` | Quit ai-cli |

Everything else is passed through, including `Ctrl+C`, `Ctrl+W`, `Ctrl+N`, `?`, `F1`, arrow keys, `Tab`, `Esc` and `Backspace`. So you can delete a word with `Ctrl+W` or type `?` in a prompt without triggering an ai-cli shortcut. Mouse movement and clicks are ignored in this mode and are never sent to Claude.

The status bar tells you which mode you're in. In terminal mode it reads `Ctrl+B: sidebar  Ctrl+Q: quit`.

---

## 6. Working with sessions

### Creating a session

`Ctrl+N` (from the sidebar) opens the dialog.

| Field | If left blank |
|---|---|
| **Name** | An automatic name such as `session-3` |
| **Directory** | The directory ai-cli was launched from |

`Enter` creates the session and `Esc` cancels. Every session runs the `claude` command.

### Switching sessions

| Key | Action |
|---|---|
| `Tab` | Next session |
| `Shift+Tab` | Previous session |

The list wraps around at both ends.

### Searching / filtering

1. Press **`/`** in the sidebar. The sidebar header turns into a `/` input.
2. Type part of a name. Matching is case-insensitive and anywhere in the name, so `api` matches `backend-api`.
3. `Tab` / `Shift+Tab` now move only between matching sessions.
4. Press **`Enter`** to talk to the selected session, or **`Esc`** to clear the filter and show every session again.

While search is open, `?` is typed into the filter instead of opening help. Use `F1` to open help.

### Killing a session

Select the session in the sidebar and press **`Ctrl+W`**. The `claude` process is stopped right away and the entry turns dim.

> ⚠️ There is **no confirmation prompt**, and any unsaved work in that Claude conversation is lost. `Ctrl+W` only works from the sidebar. In the terminal pane it's passed to Claude as a normal key.

### When Claude exits by itself

If you quit Claude from inside the session (for example with `/exit`) or it crashes, ai-cli notices. It shows any final output and dims the session. This works on all platforms.

### Quitting

`Ctrl+Q` works from anywhere. It saves the session list, stops all running `claude` processes, and restores your terminal.

---

## 7. Keyboard reference

| Key | Where it works | Action |
|---|---|---|
| `Ctrl+N` | Sidebar | Open the New Session dialog |
| `Tab` | Sidebar | Select next session (matching sessions only, while searching) |
| `Shift+Tab` | Sidebar | Select previous session |
| `Enter` | Sidebar | Move focus to the terminal pane |
| `Ctrl+B` | Terminal pane | Move focus back to the sidebar |
| `Ctrl+W` | Sidebar | Kill the selected session (no confirmation) |
| `/` | Sidebar | Start searching |
| `Esc` | Search | Clear the filter and stop searching |
| `?` | Sidebar (not while searching) | Toggle the help overlay |
| `F1` | Sidebar | Toggle the help overlay |
| `Ctrl+Q` | Anywhere | Save and quit |

**In the New Session dialog:** `Tab` moves between fields, `Enter` creates, `Esc` cancels.
**In the help overlay:** any key closes it. New output from sessions doesn't close it.

---

## 8. Saved sessions and data files

ai-cli saves your session list every time you create or kill a session, whenever a session exits, and when you quit.

| OS | File |
|---|---|
| Linux / macOS | `~/.ai-cli/sessions.json` |
| Windows | `%APPDATA%\ai-cli\sessions.json` |

The file is written atomically: ai-cli writes to `sessions.json.tmp` and then renames it, so a crash can't leave a half-written file. On Linux and macOS the file is readable only by you (mode `0600`). Example:

```json
{
  "sessions": [
    {
      "id": "3f2a9c1e-5b7d-4e21-9a0b-6c8d2e4f1a37",
      "name": "backend-api",
      "dir": "/home/me/code/backend",
      "command": "claude",
      "created_at": "2026-10-17T09:12:44Z",
      "last_active": "2026-10-17T11:03:10Z"
    }
  ]
}
```

What is **and isn't** saved:

- ✅ Session names, directories, and timestamps (all in UTC).
- ❌ Terminal output. The scrollback lives only in memory and is gone when you quit.
- ❌ Running processes. Every `claude` process is stopped on quit. After a restart, saved sessions appear **dim** and act as a history list.

To clear the list, quit ai-cli and delete `sessions.json`. If the file is unreadable or corrupt, ai-cli starts with an empty list instead of crashing.

---

## 9. Known limitations

ai-cli is at version 0.1. These limitations are intentional or not yet addressed:

| Limitation | What it means for you |
|---|---|
| **Plain-text output** | Colors, bold text and cursor movement are removed. Claude Code's live redraws, such as spinners and progress lines, can appear as repeated or merged lines. Run `claude` in a normal terminal when you need a pixel-perfect view. |
| **Fixed 80×24 terminal size** | Claude is told the terminal is 80 columns by 24 rows whatever the pane size, so long lines wrap at 80 columns. |
| **No scrolling** | The pane always shows the newest output. Older lines can't be scrolled to, and only the last 1,000 lines are kept. |
| **Dead sessions stay dead** | Dim sessions can't be restarted or removed from inside the app. Create a new session, or edit `sessions.json` while ai-cli is closed. |
| **No kill confirmation** | `Ctrl+W` in the sidebar acts immediately. |
| **`Ctrl+B` is reserved** | It can't be sent to Claude while the terminal pane is focused. |
| **Spawn errors are silent** | If `claude` can't be started, the new session is dim with no output and no error message. |

---

## 10. Troubleshooting

### A new session is dim immediately / shows no output

ai-cli couldn't start `claude`.

1. Open a normal terminal **in the same directory** and run `claude`. If that fails, fix your Claude Code install first.
2. Check that the directory you typed in the dialog exists. On Linux and macOS, if it can't be entered, `claude` starts in the launch directory instead.
3. **Windows:** run `where claude` in PowerShell. If the only result ends in `.cmd` (an npm install), ai-cli can't start it. Install Claude Code with the native Windows installer so `claude.exe` is on your `PATH`. A `claude.exe` in the current folder, or in a relative `PATH` entry such as `.\bin`, is deliberately ignored.

### Keys I type don't reach Claude

Look at the status bar. If it shows the long shortcut list, the **sidebar** is focused. Select the session and press `Enter`. Also check that the session isn't dim: keys sent to a stopped session are discarded.

### I pressed `?` / `Ctrl+W` and something unexpected happened

In the sidebar these are ai-cli shortcuts. `?` opens help and `Ctrl+W` kills the session. Press `Enter` first so they go to Claude.

### The output looks garbled or repeated

This is the plain-text limitation described in [Known limitations](#9-known-limitations). The Claude session itself is fine; only the display is simplified.

### The screen is messed up after ai-cli exits

This can happen if ai-cli was force-killed instead of quit with `Ctrl+Q`. Run `reset` on Linux or macOS, or open a new terminal tab.

### My sessions disappeared

Check that the data file in [Saved sessions](#8-saved-sessions-and-data-files) exists. If it was corrupt, ai-cli started with an empty list. On Windows, `%APPDATA%` must be set. If it isn't, the file goes to `.\ai-cli\` in the launch directory, and the same fallback applies to `HOME` on Linux and macOS.

### Reporting a bug

For security problems, follow [SECURITY.md](../SECURITY.md) and report privately instead of opening an issue. For anything else, open an issue at <https://github.com/s81/ai-cli/issues>. Include your OS, terminal app, the output of `claude --version`, and the steps that cause the problem.

---

## 11. Uninstalling

1. Delete the `ai-cli` binary, or `ai-cli.exe` on Windows.
2. Optionally delete saved data: `~/.ai-cli/` on Linux and macOS, or `%APPDATA%\ai-cli\` on Windows.

ai-cli makes no other changes to your system.

---

## 12. Building from source

**Prerequisites:** CMake 3.21+, a C++17 compiler (GCC, Clang, or MSVC 2019+), and Git. Dependencies (FTXUI, nlohmann-json, GoogleTest) are fetched automatically through the bundled vcpkg submodule.

```bash
git clone --recurse-submodules https://github.com/s81/ai-cli.git
```

```bash
cd ai-cli && ./vcpkg/bootstrap-vcpkg.sh
```

On Windows, run `.\vcpkg\bootstrap-vcpkg.bat` instead.

```bash
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=vcpkg/scripts/buildsystems/vcpkg.cmake -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build build --config Release
```

The binary is written to `build/ai-cli`, or `build\Release\ai-cli.exe` on Windows. To run the unit tests:

```bash
ctest --test-dir build --output-on-failure --build-config Release
```
