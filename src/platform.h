#pragma once
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <random>
#include <string>

#ifdef _WIN32
#  include <windows.h>
#else
#  include <csignal>
#endif

inline std::filesystem::path config_dir() {
#ifdef _WIN32
    const char* appdata = std::getenv("APPDATA");
    return std::filesystem::path(appdata ? appdata : ".") / "ai-cli";
#else
    const char* home = std::getenv("HOME");
    return std::filesystem::path(home ? home : ".") / ".ai-cli";
#endif
}

inline bool is_pid_alive(int pid) {
    if (pid <= 0) return false;
#ifdef _WIN32
    HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
    if (!h) return false;
    DWORD result = WaitForSingleObject(h, 0);
    CloseHandle(h);
    return result == WAIT_TIMEOUT;
#else
    return ::kill(static_cast<pid_t>(pid), 0) == 0;
#endif
}

inline std::string generate_uuid() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> dist;
    uint32_t p1 = dist(gen);
    uint32_t p2 = dist(gen);
    uint32_t p3 = (dist(gen) & 0x0FFFFFFFu) | 0x40000000u;
    uint32_t p4 = (dist(gen) & 0x3FFFFFFFu) | 0x80000000u;
    uint32_t p5 = dist(gen);
    char buf[37];
    std::snprintf(buf, sizeof(buf),
        "%08x-%04x-%04x-%04x-%04x%08x",
        p1,
        (p2 >> 16) & 0xFFFF, p2 & 0xFFFF,
        (p3 >> 16) & 0xFFFF,
        p4 & 0xFFFF, p5);
    return std::string(buf);
}

inline std::string current_iso8601() {
    auto now = std::chrono::system_clock::now();
    auto tt  = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
#ifdef _WIN32
    gmtime_s(&tm_buf, &tt);
#else
    gmtime_r(&tt, &tm_buf);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_buf);
    return std::string(buf);
}
