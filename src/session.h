#pragma once
#include "pty/pty.h"
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

enum class SessionState { Running, Dead };

struct Session {
    std::string  id;
    std::string  name;
    std::string  dir;
    std::string  command   = "claude";
    SessionState state     = SessionState::Dead;
    std::string  created_at;
    std::string  last_active;

    static constexpr size_t MAX_LINES = 1000;
    std::deque<std::string> lines;
    mutable std::mutex lines_mutex_;

    std::unique_ptr<Pty> pty; // owns the PTY handle; null for dead/stored sessions

    // Appends raw PTY bytes: strips ANSI, normalizes line endings,
    // splits into lines, caps ring buffer at MAX_LINES.
    void append_output(std::string_view raw);

    Session() = default;
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    Session(Session&&) noexcept;
    Session& operator=(Session&&) noexcept;

private:
    std::string partial_; // incomplete last line carried across calls
};
