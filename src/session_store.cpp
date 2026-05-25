#include "session_store.h"
#include <fstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace SessionStore {

std::vector<std::unique_ptr<Session>> load(const std::filesystem::path& path) {
    std::vector<std::unique_ptr<Session>> result;
    if (!std::filesystem::exists(path)) return result;

    std::ifstream f(path);
    if (!f.is_open()) return result;

    json j;
    try { f >> j; }
    catch (...) { return result; }

    if (!j.contains("sessions") || !j["sessions"].is_array()) return result;

    for (const auto& item : j["sessions"]) {
        auto s         = std::make_unique<Session>();
        s->id          = item.value("id",          "");
        s->name        = item.value("name",        "");
        s->dir         = item.value("dir",         "");
        s->command     = item.value("command",     "claude");
        s->created_at  = item.value("created_at",  "");
        s->last_active = item.value("last_active", "");
        s->state       = SessionState::Dead;
        result.push_back(std::move(s));
    }
    return result;
}

void save(const std::filesystem::path& path,
          const std::vector<std::unique_ptr<Session>>& sessions) {
    json j;
    j["sessions"] = json::array();
    for (const auto& s : sessions) {
        j["sessions"].push_back({
            {"id",          s->id},
            {"name",        s->name},
            {"dir",         s->dir},
            {"command",     s->command},
            {"created_at",  s->created_at},
            {"last_active", s->last_active},
        });
    }

    std::filesystem::create_directories(path.parent_path());
    auto tmp = path; tmp += ".tmp";
    { std::ofstream f(tmp); f << j.dump(2); }
    std::filesystem::rename(tmp, path);
}

} // namespace SessionStore
