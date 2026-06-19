#include "session.h"
#include <mutex>
#include <sstream>

Session::Session(Session&& o) noexcept
    : id(std::move(o.id))
    , name(std::move(o.name))
    , dir(std::move(o.dir))
    , command(std::move(o.command))
    , state(o.state)
    , created_at(std::move(o.created_at))
    , last_active(std::move(o.last_active))
    , lines(std::move(o.lines))
    , pty(std::move(o.pty))
    , partial_(std::move(o.partial_))
    // lines_mutex_ is default-constructed: it belongs to this object, not the source
{}

Session& Session::operator=(Session&& o) noexcept {
    if (this != &o) {
        // lock both in a consistent order to avoid deadlock
        std::lock(lines_mutex_, o.lines_mutex_);
        std::lock_guard<std::mutex> lk1(lines_mutex_,   std::adopt_lock);
        std::lock_guard<std::mutex> lk2(o.lines_mutex_, std::adopt_lock);
        id          = std::move(o.id);
        name        = std::move(o.name);
        dir         = std::move(o.dir);
        command     = std::move(o.command);
        state       = o.state;
        created_at  = std::move(o.created_at);
        last_active = std::move(o.last_active);
        lines       = std::move(o.lines);
        pty         = std::move(o.pty);
        partial_    = std::move(o.partial_);
    }
    return *this;
}

static std::string strip_ansi(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    bool esc = false, csi = false;
    for (size_t i = 0; i < in.size(); ++i) {
        unsigned char c = in[i];
        if (c == '\x1b') {
            esc = true; csi = false;
        } else if (esc) {
            if (c == '[') { csi = true; esc = false; }
            else            esc = false;
        } else if (csi) {
            if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) csi = false;
        } else {
            if (c >= 32 || c == '\n' || c == '\r' || c == '\t')
                out += static_cast<char>(c);
        }
    }
    return out;
}

void Session::append_output(std::string_view raw) {
    std::string cleaned = strip_ansi(std::string(raw));

    // normalize \r\n -> \n, drop lone \r
    std::string normalized;
    normalized.reserve(cleaned.size());
    for (size_t i = 0; i < cleaned.size(); ++i) {
        if (cleaned[i] == '\r') continue; // drop all \r; \r\n becomes \n via the \n itself
        normalized += cleaned[i];
    }

    // prepend any unfinished line from the previous call
    normalized = partial_ + normalized;
    partial_.clear();

    std::istringstream ss(normalized);
    std::string line;
    bool ends_with_newline = !normalized.empty() && normalized.back() == '\n';

    std::lock_guard<std::mutex> lock(lines_mutex_);
    while (std::getline(ss, line)) {
        lines.push_back(std::move(line));
        if (lines.size() > MAX_LINES) lines.pop_front();
    }

    // if the last chunk had no trailing newline, pull the last entry back into partial_
    if (!ends_with_newline && !lines.empty()) {
        partial_ = std::move(lines.back());
        lines.pop_back();
    }
}
