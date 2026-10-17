# Security Policy

## Reporting a vulnerability

Please **do not open a public issue** for security problems. Report them privately through GitHub's [private vulnerability reporting](https://github.com/s81/ai-cli/security/advisories/new) for this repository.

Include the affected version or commit, your OS, and steps to reproduce. You can expect an acknowledgement within a few days. A fix or mitigation plan follows once the issue is confirmed.

## Supported versions

Only the latest release receives security fixes.

## Threat model

ai-cli is a local terminal app. It opens no network ports, has no accounts, and stores no credentials. Claude Code handles authentication itself. ai-cli's job is to start `claude` processes, relay keystrokes to them, and display their output. The defenses below target the places where untrusted data or code can enter.

| Threat | Defense |
|---|---|
| **Terminal escape injection.** Claude's output can include text from untrusted sources such as repository files, web pages, or tool results. Escape sequences in that text could, for example, write to your clipboard (OSC 52), change the window title, or redraw your screen if passed to your terminal. | Every escape and control sequence (CSI, OSC, DCS/APC/PM/SOS, charset designations, 2-byte escapes, UTF-8-encoded C1 controls, other control bytes) is removed before display. The parser keeps its state across reads, so a sequence split between two reads is still removed. An unterminated sequence is abandoned after 4 KiB, so it can't hide later output. |
| **Memory exhaustion from output.** A program may print a huge amount of text, or a line with no end. | 1,000 lines per session, at most 8 KiB per line. Longer lines are split. |
| **Binary planting (Windows).** `CreateProcess` would look for `claude.exe` in ai-cli's own folder and in the current directory before `PATH`. | `claude.exe` is looked up only in **absolute** `PATH` entries and started by its full path. Relative `PATH` entries are ignored. |
| **Cross-session access.** A new session could inherit handles to earlier sessions' terminals. | Unix: PTY master descriptors are close-on-exec. Windows: no handles are inherited. |
| **Orphaned processes.** Killing a session could leave Claude's child processes (tools, MCP servers) running. | Unix: the whole process group is signaled (SIGTERM, then SIGKILL) and reaped. Windows: each session runs in a Job Object that kills the whole process tree, even if ai-cli crashes. |
| **Tampered `sessions.json`.** | Treated as untrusted input. Files over 4 MiB are ignored, malformed entries are skipped, and names are stripped of control characters. The stored `command` field is never executed: ai-cli always starts the fixed `claude` command. |
| **Config file disclosure / symlink attacks.** | Unix: the file is created with mode `0600`, and its directory with `0700` when ai-cli creates it. Writes go to a fresh temp file opened with `O_EXCL \| O_NOFOLLOW`, then are renamed into place. If `HOME` is unset or relative, ai-cli uses the system password database instead of the current directory. |
| **Supply chain.** | Dependencies are pinned through the vcpkg submodule commit. GitHub Actions are pinned to commit SHAs (Dependabot keeps them updated), workflow tokens are read-only except for the release step, and every release ships `SHA256SUMS.txt`. |

### Out of scope

- **What Claude Code itself does.** ai-cli gives Claude exactly the keystrokes you type, in the directory you choose. Claude's file and command permissions are configured in Claude Code.
- **An attacker who already runs code as your user.** Such an attacker can read your files and control your terminal regardless of ai-cli.
- **Your `PATH`.** On Linux and macOS, `claude` is resolved through your `PATH` like any command you type. Don't put `.` or other writable, untrusted directories on it.

## Verifying a download

Each release includes `SHA256SUMS.txt`. Download it next to the binary and check the binary against it.

Linux:

```bash
sha256sum --check --ignore-missing SHA256SUMS.txt
```

macOS:

```bash
shasum -a 256 --check --ignore-missing SHA256SUMS.txt
```

Windows (PowerShell). Compare the output with the matching line in `SHA256SUMS.txt`:

```powershell
Get-FileHash .\ai-cli-windows-x86_64.exe -Algorithm SHA256
```

The binaries aren't code-signed. The checksum confirms that your file matches what the release workflow built.
