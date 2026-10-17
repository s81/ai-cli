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
    // Signal every reader first so they all wind down in parallel, then join.
    for (auto& s : sessions_) s->state = SessionState::Dead;
    for (auto& [s, t] : io_threads_)
        if (t.joinable()) t.join();
    io_threads_.clear();
    for (auto& s : sessions_)
        if (s->pty) s->pty->kill();
}

void App::save() {
    SessionStore::save(store_path_, sessions_);
}

void App::create_session(const std::string& name, const std::string& dir) {
    auto s        = std::make_unique<Session>();
    s->id         = generate_uuid();
    std::string clean = sanitize_display_name(name);
    s->name       = clean.empty() ? "session-" + std::to_string(sessions_.size() + 1) : clean;
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
    stop_io_thread(s.get());
    if (s->pty) s->pty->kill();
    save();
}

void App::cycle_next() {
    if (sessions_.empty()) return;
    active_ = (active_ + 1) % sessions_.size();
}

void App::start_io_thread(Session* s) {
    io_threads_[s] = std::thread([this, s] {
        auto on_output = [&](const std::string& data) {
            s->append_output(data);
            {
                std::lock_guard<std::mutex> lock(s->lines_mutex_);
                s->last_active = current_iso8601();
            }
            screen_.PostEvent(Event::Custom);
        };
        while (s->state == SessionState::Running) {
            auto data = s->pty->read(100);
            if (!data.empty()) on_output(data);
            if (!s->pty->running()) {
                // Pick up whatever the child wrote right before exiting.
                for (auto rest = s->pty->read(0); !rest.empty(); rest = s->pty->read(0))
                    on_output(rest);
                s->state = SessionState::Dead;
                save_pending_.store(true, std::memory_order_release);
                screen_.PostEvent(Event::Custom);
                break;
            }
        }
    });
}

void App::stop_io_thread(Session* s) {
    s->state = SessionState::Dead; // reader exits within one read timeout
    auto it = io_threads_.find(s);
    if (it == io_threads_.end()) return;
    if (it->second.joinable()) it->second.join();
    io_threads_.erase(it);
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

    // Advance active_ to the next/prev session whose name matches search_query_.
    // When search_query_ is empty every session matches, so behaviour is identical
    // to a plain index wrap — no special-casing needed at the call site.
    auto advance_active = [&](bool forward) {
        size_t n = sessions_.size();
        if (n == 0) return;
        for (size_t step = 1; step < n; ++step) {
            size_t candidate = forward
                ? (active_ + step) % n
                : (active_ + n - step) % n;
            if (session_matches(sessions_[candidate]->name, search_query_)) {
                active_ = candidate;
                return;
            }
        }
        // No other session matches; stay on active_ (it may or may not match).
    };

    auto sidebar_container = Container::Vertical({ search_input, list });
    auto layout = Container::Horizontal({ sidebar_container, term });

    auto root = CatchEvent(layout, [&](Event ev) -> bool {
        // ── Global shortcuts ────────────────────────────────────────────
        if (ev == Event::CtrlQ) {
            save();
            screen_.ExitLoopClosure()();
            return true;
        }
        if (ev == Event::Custom) {
            if (save_pending_.load(std::memory_order_acquire)) {
                save_pending_.store(false, std::memory_order_relaxed);
                save();
            }
            return false; // let Event::Custom propagate to trigger re-render
        }
        // ── PTY pane focused: forward all keys to PTY ───────────────────
        // Only Ctrl+B and Ctrl+Q are reserved here; everything else (Ctrl+W
        // word-delete, '?', F1, Ctrl+N ...) belongs to the program in the PTY.
        if (pty_focused) {
            // Ctrl+B (\x02) — return focus to sidebar
            if (ev == Event::Special("\x02")) {
                pty_focused = false;
                return true;
            }
            // Mouse and terminal reports are not keystrokes; don't leak them.
            if (ev.is_mouse() || ev.is_cursor_position() || ev.is_cursor_shape())
                return true;
            if (active_ < sessions_.size() && sessions_[active_]->pty &&
                sessions_[active_]->state == SessionState::Running)
            {
                sessions_[active_]->pty->write(ev.input());
            }
            return true;
        }

        // ── Sidebar focused ─────────────────────────────────────────────
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
        // F1, or ? when not typing a search query
        if (ev == Event::F1 || (ev == Event::Character('?') && !search_mode_)) {
            show_help_ = !show_help_;
            return true;
        }
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
            advance_active(true);
            return true;
        }
        if (ev == Event::TabReverse) {
            advance_active(false);
            return true;
        }
        if (ev == Event::Return && !sessions_.empty()) {
            pty_focused = true;
            return true;
        }

        // Only the search input consumes the rest, and only while searching;
        // otherwise stray keys would silently edit a hidden filter.
        return !search_mode_;
    });

    auto renderer = Renderer(root, [&] {
        std::string status = search_mode_
            ? " Esc: exit search"
            : (pty_focused
               ? " Ctrl+B: sidebar  Ctrl+Q: quit"
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
                text(" In the PTY pane only ^B and ^Q are") | dim,
                text(" reserved; other keys go to Claude.") | dim,
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
            // PTY output arrives as Event::Custom; let it through to redraw
            // and flush saves without dismissing the overlay.
            if (ev == Event::Custom) return false;
            if (ev.is_mouse() || ev.is_cursor_position() || ev.is_cursor_shape())
                return true;
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
