#include "terminal_view.h"
#include <ftxui/dom/elements.hpp>
#include <mutex>

using namespace ftxui;

ftxui::Component MakeTerminalView(
    const std::vector<std::unique_ptr<Session>>& sessions,
    const size_t& active)
{
    return Renderer([&sessions, &active] {
        if (sessions.empty()) {
            return text(" No sessions. Press Ctrl+N to create one.") | dim | center;
        }
        if (active >= sessions.size()) return text("") | flex;

        const auto& s = sessions[active];
        Elements lines;
        {
            std::lock_guard<std::mutex> lock(s->lines_mutex_);
            lines.reserve(s->lines.size());
            for (const auto& line : s->lines) {
                lines.push_back(text(line));
            }
        }
        if (lines.empty()) lines.push_back(text(""));

        return vbox(std::move(lines)) | yframe | flex;
    });
}
