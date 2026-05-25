#include "session.h"
#include <sstream>

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
