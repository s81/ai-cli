#include "app.h"
#include "ui/session_list.h"
#include "ui/terminal_view.h"
#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>

using namespace ftxui;

App::App()
    : store_path_(config_dir() / "sessions.json")
{
    sessions_ = SessionStore::load(store_path_);
    for (auto& s : sessions_) s->state = SessionState::Dead;
}

App::~App() {
    for (auto& s : sessions_)
        if (s->pty && s->state == SessionState::Running) s->pty->kill();
}

void App::save() {
    SessionStore::save(store_path_, sessions_);
}

void App::create_session(const std::string& name, const std::string& dir) {
    auto s        = std::make_unique<Session>();
    s->id         = generate_uuid();
    s->name       = name.empty() ? "session-" + std::to_string(sessions_.size() + 1) : name;
    s->dir        = dir;
    s->command    = "claude";
    s->created_at = current_iso8601();
    s->last_active= s->created_at;
    s->pty        = Pty::create();

    if (!s->pty->spawn(s->command, s->dir, 80, 24)) {
        s->state = SessionState::Dead;
        sessions_.push_back(std::move(s));
        return;
    }
    s->state = SessionState::Running;
    active_  = sessions_.size();
    Session* raw = s.get();
    sessions_.push_back(std::move(s));
    save();
    start_io_thread(raw);
}

void App::kill_active_session() {
    if (active_ >= sessions_.size()) return;
    auto& s = sessions_[active_];
    if (s->pty) s->pty->kill();
    s->state = SessionState::Dead;
    save();
}

void App::cycle_next() {
    if (sessions_.empty()) return;
    active_ = (active_ + 1) % sessions_.size();
}

void App::start_io_thread(Session* s) {
    std::thread([this, s] {
        while (s->state == SessionState::Running) {
            auto data = s->pty->read(100);
            if (!data.empty()) {
                s->append_output(data);
                s->last_active = current_iso8601();
                screen_.PostEvent(Event::Custom);
            }
            if (!s->pty->running()) {
                s->state = SessionState::Dead;
                screen_.PostEvent(Event::Custom);
                break;
            }
        }
    }).detach();
}

void App::run() {
    auto list = MakeSessionList(sessions_, active_, search_query_,
                                [this] { show_dialog_ = true; });
    auto term = MakeTerminalView(sessions_, active_);

    Component dlg_name_input = Input(&dlg_name_, "Session name");
    Component dlg_dir_input  = Input(&dlg_dir_,  "Directory (blank = $PWD)");
    auto dialog_form = Container::Vertical({ dlg_name_input, dlg_dir_input });

    Component search_input = Input(&search_query_, "filter...");

    bool pty_focused = false;

    auto sidebar_container = Container::Vertical({ search_input, list });
    auto layout = Container::Horizontal({ sidebar_container, term });

    auto root = CatchEvent(layout, [&](Event ev) -> bool {
        // ── Global shortcuts ────────────────────────────────────────────
        if (ev == Event::CtrlQ) {
            screen_.ExitLoopClosure()();
            return true;
        }
        // Ctrl+N (\x0e)
        if (ev == Event::Special("\x0e")) {
            dlg_name_.clear(); dlg_dir_.clear();
            show_dialog_ = true;
            return true;
        }
        // Ctrl+W (\x17)
        if (ev == Event::Special("\x17")) {
            kill_active_session();
            return true;
        }
        // F1 or ?
        if (ev == Event::F1 || ev == Event::Character('?')) {
            show_help_ = !show_help_;
            return true;
        }

        // ── PTY pane focused: forward all keys to PTY ───────────────────
        if (pty_focused) {
            // Ctrl+B (\x02) — return focus to sidebar
            if (ev == Event::Special("\x02")) {
                pty_focused = false;
                return true;
            }
            if (active_ < sessions_.size() && sessions_[active_]->pty &&
                sessions_[active_]->state == SessionState::Running)
            {
                sessions_[active_]->pty->write(ev.character());
            }
            return true;
        }

        // ── Sidebar focused ─────────────────────────────────────────────
        if (ev == Event::Character('/') && !search_mode_) {
            search_mode_ = true;
            search_query_.clear();
            return true;
        }
        if (search_mode_ && ev == Event::Escape) {
            search_mode_ = false;
            search_query_.clear();
            return true;
        }
        if (ev == Event::Tab) {
            if (!sessions_.empty()) active_ = (active_ + 1) % sessions_.size();
            return true;
        }
        if (ev == Event::TabReverse) {
            if (!sessions_.empty())
                active_ = (active_ + sessions_.size() - 1) % sessions_.size();
            return true;
        }
        if (ev == Event::Return && !sessions_.empty()) {
            pty_focused = true;
            return true;
        }

        return false;
    });

    auto renderer = Renderer(root, [&] {
        std::string status = search_mode_
            ? " Esc: exit search"
            : (pty_focused
               ? " Ctrl+B: sidebar  Ctrl+W: kill  Ctrl+Q: quit"
               : " ^N new  ^W kill  /: search  Tab: next  Enter: focus  ?: help  ^Q quit");

        Element sidebar_content;
        if (search_mode_) {
            sidebar_content = vbox({
                hbox({ text(" /"), search_input->Render() }),
                separator(),
                list->Render(),
            });
        } else {
            sidebar_content = vbox({
                text(" Sessions ") | bold,
                separator(),
                list->Render(),
            });
        }

        auto body = hbox({
            sidebar_content | size(WIDTH, EQUAL, 22) | border,
            term->Render() | border | flex,
        });

        Element ui = vbox({
            text(" ai-cli  v0.1.0") | bold | inverted,
            body | flex,
            text(status) | inverted,
        });

        if (show_dialog_) {
            auto dlg = vbox({
                text(" New Session ") | bold | center,
                separator(),
                hbox({ text(" Name:  "), dlg_name_input->Render() }),
                hbox({ text(" Dir:   "), dlg_dir_input->Render()  }),
                separator(),
                text(" Enter: create   Esc: cancel") | dim | center,
            }) | border | size(WIDTH, EQUAL, 50);
            ui = dbox({ ui, dlg | center });
        }

        if (show_help_) {
            auto help = vbox({
                text(" Keyboard Reference ") | bold | center,
                separator(),
                text(" Ctrl+N     New session"),
                text(" Ctrl+W     Kill active session"),
                text(" Tab        Next session in list"),
                text(" Shift+Tab  Previous session"),
                text(" Enter      Focus PTY pane"),
                text(" Ctrl+B     Return to sidebar"),
                text(" /          Search sessions"),
                text(" Escape     Exit search"),
                text(" F1 / ?     Toggle this help"),
                text(" Ctrl+Q     Quit"),
                separator(),
                text(" Press any key to close") | dim | center,
            }) | border | size(WIDTH, EQUAL, 42);
            ui = dbox({ ui, help | center });
        }

        return ui;
    });

    // Dialog + help key handling (outer layer)
    auto top = CatchEvent(renderer, [&](Event ev) -> bool {
        if (show_help_) {
            show_help_ = false;
            return true;
        }
        if (show_dialog_) {
            if (ev == Event::Return) {
                show_dialog_ = false;
                create_session(dlg_name_, dlg_dir_);
                return true;
            }
            if (ev == Event::Escape) {
                show_dialog_ = false;
                return true;
            }
            return dialog_form->OnEvent(ev);
        }
        return false;
    });

    screen_.Loop(top);
}
