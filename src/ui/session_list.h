#pragma once
#include "../session.h"
#include <ftxui/component/component.hpp>
#include <algorithm>
#include <cctype>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// Case-insensitive substring match; empty query matches everything.
inline bool session_matches(const std::string& name, const std::string& q) {
    if (q.empty()) return true;
    // ::tolower on a negative char (any UTF-8 byte >= 0x80) is undefined
    // behaviour; go through unsigned char.
    auto lower = [](char c) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    };
    std::string low_name = name, low_q = q;
    std::transform(low_name.begin(), low_name.end(), low_name.begin(), lower);
    std::transform(low_q.begin(),   low_q.end(),   low_q.begin(),   lower);
    return low_name.find(low_q) != std::string::npos;
}

// selected: index into sessions (unfiltered).
// query: live filter string; empty = show all.
ftxui::Component MakeSessionList(
    const std::vector<std::unique_ptr<Session>>& sessions,
    size_t& selected,
    const std::string& query,
    std::function<void()> on_new);
