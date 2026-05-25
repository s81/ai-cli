#pragma once
#include "session.h"
#include <filesystem>
#include <memory>
#include <vector>

namespace SessionStore {
    std::vector<std::unique_ptr<Session>> load(const std::filesystem::path& path);
    void save(const std::filesystem::path& path,
              const std::vector<std::unique_ptr<Session>>& sessions);
}
