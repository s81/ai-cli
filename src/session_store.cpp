#include "session_store.h"
#include <cstdint>
#include <fstream>
#include <nlohmann/json.hpp>
#include <system_error>

#ifndef _WIN32
#  include <cerrno>
#  include <fcntl.h>
#  include <unistd.h>
#endif

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace SessionStore {

// A legitimate store is a few KB; refuse to parse anything absurd.
constexpr std::uintmax_t MAX_STORE_BYTES = 4 * 1024 * 1024;

std::vector<std::unique_ptr<Session>> load(const fs::path& path) {
    std::vector<std::unique_ptr<Session>> result;
    std::error_code ec;
    if (!fs::is_regular_file(path, ec)) return result;
    auto size = fs::file_size(path, ec);
    if (ec || size > MAX_STORE_BYTES) return result;

    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return result;

    json j;
    try { f >> j; }
    catch (...) { return result; }

    if (!j.is_object() || !j.contains("sessions") || !j["sessions"].is_array())
        return result;

    // The file is untrusted input: skip any entry with the wrong shape rather
    // than letting a type error escape and abort startup.
    for (const auto& item : j["sessions"]) {
        if (!item.is_object()) continue;
        try {
            auto s         = std::make_unique<Session>();
            s->id          = item.value("id",          "");
            s->name        = sanitize_display_name(item.value("name", ""));
            s->dir         = item.value("dir",         "");
            // Informational only: App always spawns the fixed "claude"
            // command and never executes a command read from this file.
            s->command     = item.value("command",     "claude");
            s->created_at  = item.value("created_at",  "");
            s->last_active = item.value("last_active", "");
            s->state       = SessionState::Dead;
            result.push_back(std::move(s));
        } catch (const json::exception&) {
            continue;
        }
    }
    return result;
}

// Writes data to a fresh file readable only by the current user. Refuses to
// follow a pre-existing file or symlink at tmp.
static bool write_private_file(const fs::path& tmp, const std::string& data) {
    std::error_code ec;
    fs::remove(tmp, ec);
#ifdef _WIN32
    // %APPDATA% is already restricted to the user by its ACL.
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    f << data;
    return static_cast<bool>(f);
#else
    int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0) return false;
    const char* p = data.data();
    size_t left = data.size();
    while (left > 0) {
        ssize_t n = ::write(fd, p, left);
        if (n < 0) {
            if (errno == EINTR) continue;
            ::close(fd);
            return false;
        }
        p += n;
        left -= static_cast<size_t>(n);
    }
    bool ok = ::fsync(fd) == 0;
    return ::close(fd) == 0 && ok;
#endif
}

void save(const fs::path& path,
          const std::vector<std::unique_ptr<Session>>& sessions) {
    json j;
    j["sessions"] = json::array();
    for (const auto& s : sessions) {
        std::string last_active;
        {
            std::lock_guard<std::mutex> lock(s->lines_mutex_);
            last_active = s->last_active;
        }
        j["sessions"].push_back({
            {"id",          s->id},
            {"name",        s->name},
            {"dir",         s->dir},
            {"command",     s->command},
            {"created_at",  s->created_at},
            {"last_active", last_active},
        });
    }
    // replace, don't throw, on invalid UTF-8 (e.g. a typed directory name)
    std::string data = j.dump(2, ' ', false, json::error_handler_t::replace);

    // Saving must never take the app down: in-memory state is authoritative.
    std::error_code ec;
    auto dir = path.parent_path();
    if (!dir.empty()) {
        bool created = fs::create_directories(dir, ec);
#ifndef _WIN32
        // Session names and project paths are nobody else's business. Only
        // touch a directory we just made; never re-mode an existing one.
        if (created)
            fs::permissions(dir, fs::perms::owner_all, fs::perm_options::replace, ec);
#else
        (void)created;
#endif
    }
    auto tmp = path; tmp += ".tmp";
    if (!write_private_file(tmp, data)) {
        fs::remove(tmp, ec);
        return;
    }
    fs::rename(tmp, path, ec);
    if (ec) fs::remove(tmp, ec);
}

} // namespace SessionStore
