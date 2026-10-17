#pragma once
#include "pty/pty.h"
#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

enum class SessionState { Running, Dead };

// Makes a string safe to draw in the UI: drops control bytes (including ESC
// and UTF-8 encoded C1 controls) and truncates to max_bytes on a UTF-8
// boundary. Used for session names, which may come from a tampered file.
std::string sanitize_display_name(std::string_view in, size_t max_bytes = 64);

struct Session {
    std::string  id;
    std::string  name;
    std::string  dir;
    std::string  command   = "claude";
    std::atomic<SessionState> state{SessionState::Dead}; // written by the I/O thread
    std::string  created_at;
    std::string  last_active; // guarded by lines_mutex_

    static constexpr size_t MAX_LINES      = 1000;
    static constexpr size_t MAX_LINE_BYTES = 8192; // longer lines are split
    std::deque<std::string> lines;
    mutable std::mutex lines_mutex_; // guards lines, partial_ and last_active

    std::unique_ptr<Pty> pty; // owns the PTY handle; null for dead/stored sessions

    // Appends raw PTY bytes: strips every terminal escape/control sequence
    // (so program output can never drive the outer terminal), normalizes
    // line endings, splits into lines, caps the ring buffer at MAX_LINES.
    // Escape-sequence state carries across calls; call from one thread only.
    void append_output(std::string_view raw);

    Session() = default;
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    Session(Session&&) noexcept;
    Session& operator=(Session&&) noexcept;

private:
    std::string partial_; // incomplete last line carried across calls

    // Escape-sequence parser state (see append_output).
    unsigned char esc_state_  = 0;
    size_t        str_len_    = 0;     // bytes consumed inside an OSC/DCS string
    bool          pending_c2_ = false; // saw 0xC2, deciding if it starts a C1

    std::string sanitize(std::string_view raw);
    void push_line(std::string line); // requires lines_mutex_
};
