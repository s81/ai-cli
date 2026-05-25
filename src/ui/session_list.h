#pragma once
#include "../session.h"
#include <ftxui/component/component.hpp>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// selected: index into sessions (unfiltered).
// query: live filter string; empty = show all.
ftxui::Component MakeSessionList(
    const std::vector<std::unique_ptr<Session>>& sessions,
    size_t& selected,
    const std::string& query,
    std::function<void()> on_new);
