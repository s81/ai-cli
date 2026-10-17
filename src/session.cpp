#include "session.h"
#include <mutex>

Session::Session(Session&& o) noexcept
    : id(std::move(o.id))
    , name(std::move(o.name))
    , dir(std::move(o.dir))
    , command(std::move(o.command))
    , state(o.state.load())
    , created_at(std::move(o.created_at))
    , last_active(std::move(o.last_active))
    , lines(std::move(o.lines))
    , pty(std::move(o.pty))
    , partial_(std::move(o.partial_))
    , esc_state_(o.esc_state_)
    , str_len_(o.str_len_)
    , pending_c2_(o.pending_c2_)
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
        state       = o.state.load();
        created_at  = std::move(o.created_at);
        last_active = std::move(o.last_active);
        lines       = std::move(o.lines);
        pty         = std::move(o.pty);
        partial_    = std::move(o.partial_);
        esc_state_  = o.esc_state_;
        str_len_    = o.str_len_;
        pending_c2_ = o.pending_c2_;
    }
    return *this;
}

std::string sanitize_display_name(std::string_view in, size_t max_bytes) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(in[i]);
        if (c < 0x20 || c == 0x7F) continue;
        // U+0080..U+009F (C1 controls) encode as C2 80..C2 9F.
        if (c == 0xC2 && i + 1 < in.size()) {
            unsigned char n = static_cast<unsigned char>(in[i + 1]);
            if (n >= 0x80 && n <= 0x9F) { ++i; continue; }
        }
        out += static_cast<char>(c);
    }
    if (out.size() > max_bytes) {
        size_t cut = max_bytes;
        // don't split a multi-byte UTF-8 sequence
        while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80) --cut;
        out.resize(cut);
    }
    return out;
}

namespace {
// Parser states. Covers every ECMA-48 sequence a program can emit:
// CSI (ESC [), OSC/DCS/SOS/PM/APC strings, nF escapes with intermediate
// bytes (ESC ( B ...), and plain two-byte escapes (ESC M ...).
enum : unsigned char {
    S_NORMAL,
    S_ESC,       // after ESC
    S_ESC_INTER, // ESC + intermediate bytes 0x20-0x2F, waiting for final byte
    S_CSI,       // inside ESC [ ... until final byte 0x40-0x7E
    S_STR,       // inside OSC/DCS/SOS/PM/APC until BEL or ESC \.
    S_STR_ESC,   // saw ESC inside a string
};
// An unterminated string sequence would otherwise hide all later output.
constexpr size_t MAX_STRING_SEQ = 4096;
} // namespace

std::string Session::sanitize(std::string_view raw) {
    std::string out;
    out.reserve(raw.size());

    auto after_esc = [this](unsigned char c) {
        if (c == '[')                     esc_state_ = S_CSI;
        else if (c == ']' || c == 'P' || c == 'X' || c == '^' || c == '_') {
            esc_state_ = S_STR;
            str_len_   = 0;
        }
        else if (c >= 0x20 && c <= 0x2F)  esc_state_ = S_ESC_INTER;
        else if (c == 0x1B)               esc_state_ = S_ESC;
        else                              esc_state_ = S_NORMAL; // 2-byte escape, consumed
    };

    for (char ch : raw) {
        unsigned char c = static_cast<unsigned char>(ch);
        switch (esc_state_) {
        case S_NORMAL:
            if (pending_c2_) {
                pending_c2_ = false;
                if (c >= 0x80 && c <= 0x9F) continue; // UTF-8 encoded C1 control
                out += '\xC2';
            }
            if (c == 0x1B) { esc_state_ = S_ESC; continue; }
            if (c == 0xC2) { pending_c2_ = true; continue; }
            if (c == '\n' || c == '\r' || c == '\t' || (c >= 0x20 && c != 0x7F))
                out += static_cast<char>(c);
            continue;
        case S_ESC:
            after_esc(c);
            continue;
        case S_ESC_INTER:
            if (c >= 0x30 && c <= 0x7E)      esc_state_ = S_NORMAL;
            else if (c == 0x1B)              esc_state_ = S_ESC;
            else if (c < 0x20 || c > 0x2F)   esc_state_ = S_NORMAL;
            continue;
        case S_CSI:
            if (c >= 0x40 && c <= 0x7E)      esc_state_ = S_NORMAL;
            else if (c == 0x1B)              esc_state_ = S_ESC;
            continue;
        case S_STR:
            if (c == 0x07)                   esc_state_ = S_NORMAL;
            else if (c == 0x1B)              esc_state_ = S_STR_ESC;
            else if (++str_len_ > MAX_STRING_SEQ) esc_state_ = S_NORMAL;
            continue;
        case S_STR_ESC:
            // ESC \ terminates; any other ESC x aborts the string and starts a new escape
            if (c == '\\') esc_state_ = S_NORMAL;
            else           after_esc(c);
            continue;
        default:
            esc_state_ = S_NORMAL;
            continue;
        }
    }
    return out;
}

void Session::push_line(std::string line) {
    lines.push_back(std::move(line));
    if (lines.size() > MAX_LINES) lines.pop_front();
}

void Session::append_output(std::string_view raw) {
    std::string cleaned = sanitize(raw);

    std::lock_guard<std::mutex> lock(lines_mutex_);
    for (char c : cleaned) {
        if (c == '\r') continue; // drop all \r; \r\n becomes \n via the \n itself
        if (c == '\n') {
            push_line(std::move(partial_));
            partial_.clear();
            continue;
        }
        partial_ += c;
        // bound memory even if the program never emits a newline
        if (partial_.size() >= MAX_LINE_BYTES) {
            push_line(std::move(partial_));
            partial_.clear();
        }
    }
}
