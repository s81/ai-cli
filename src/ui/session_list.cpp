#include "session_list.h"
#include <ftxui/dom/elements.hpp>

using namespace ftxui;

ftxui::Component MakeSessionList(
    const std::vector<std::unique_ptr<Session>>& sessions,
    size_t& selected,
    const std::string& query,
    std::function<void()> on_new)
{
    return Renderer([&sessions, &selected, &query, on_new = std::move(on_new)] {
        Elements rows;

        for (size_t i = 0; i < sessions.size(); ++i) {
            const auto& s = sessions[i];
            if (!session_matches(s->name, query)) continue;

            bool is_sel  = (i == selected);
            bool is_dead = (s->state == SessionState::Dead);
            std::string label = (is_sel ? " ▶ " : "   ") + s->name;
            auto row = text(label);
            if (is_dead) row = row | dim;
            if (is_sel)  row = row | inverted;
            rows.push_back(row);
        }

        if (rows.empty()) rows.push_back(text("   (no sessions)") | dim);
        rows.push_back(separator());
        rows.push_back(text("  [+ New]") | dim);
        return vbox(std::move(rows));
    });
}
