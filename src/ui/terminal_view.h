#pragma once
#include "../session.h"
#include <ftxui/component/component.hpp>
#include <memory>
#include <vector>

ftxui::Component MakeTerminalView(
    const std::vector<std::unique_ptr<Session>>& sessions,
    const size_t& active);
