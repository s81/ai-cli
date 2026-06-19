#pragma once
#include "session.h"
#include "session_store.h"
#include "platform.h"
#include "pty/pty.h"
#include <ftxui/component/screen_interactive.hpp>
#include <atomic>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

class App {
public:
    App();
    ~App();

    void run(); // blocks until quit

private:
    ftxui::ScreenInteractive screen_ = ftxui::ScreenInteractive::Fullscreen();
    std::vector<std::unique_ptr<Session>> sessions_;
    size_t active_ = 0;
    std::filesystem::path store_path_;

    std::atomic<bool> save_pending_{false};

    bool        show_dialog_ = false;
    std::string dlg_name_;
    std::string dlg_dir_;

    bool        search_mode_  = false;
    std::string search_query_;

    bool show_help_ = false;

    void create_session(const std::string& name, const std::string& dir);
    void kill_active_session();
    void save();
    void start_io_thread(Session* s);
    void cycle_next();
};
