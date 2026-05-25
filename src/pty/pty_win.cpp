#include "pty.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <vector>

#include <atomic>
#include <string>

class PtyWin final : public Pty {
    HPCON  con_        = nullptr;
    HANDLE read_pipe_  = INVALID_HANDLE_VALUE;
    HANDLE write_pipe_ = INVALID_HANDLE_VALUE;
    HANDLE proc_       = INVALID_HANDLE_VALUE;
    DWORD  pid_        = 0;
    std::atomic<bool> running_{false};

public:
    ~PtyWin() override { kill(); }

    bool spawn(const std::string& cmd, const std::string& dir,
               int cols, int rows) override {
        HANDLE stdin_r, stdin_w, stdout_r, stdout_w;
        if (!CreatePipe(&stdin_r, &stdin_w, nullptr, 0)) return false;
        if (!CreatePipe(&stdout_r, &stdout_w, nullptr, 0)) {
            CloseHandle(stdin_r); CloseHandle(stdin_w);
            return false;
        }

        COORD size{ static_cast<SHORT>(cols), static_cast<SHORT>(rows) };
        HRESULT hr = CreatePseudoConsole(size, stdin_r, stdout_w, 0, &con_);
        CloseHandle(stdin_r);
        CloseHandle(stdout_w);
        if (FAILED(hr)) { CloseHandle(stdin_w); CloseHandle(stdout_r); return false; }

        write_pipe_ = stdin_w;
        read_pipe_  = stdout_r;

        SIZE_T attr_size = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &attr_size);
        std::vector<BYTE> attr_buf(attr_size);
        auto lpAttr = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attr_buf.data());
        InitializeProcThreadAttributeList(lpAttr, 1, 0, &attr_size);
        UpdateProcThreadAttribute(lpAttr, 0,
            PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, con_, sizeof(con_), nullptr, nullptr);

        STARTUPINFOEXW si{};
        si.StartupInfo.cb = sizeof(STARTUPINFOEXW);
        si.lpAttributeList = lpAttr;

        std::wstring wcmd(cmd.begin(), cmd.end());
        std::wstring wdir = dir.empty() ? L"" : std::wstring(dir.begin(), dir.end());

        PROCESS_INFORMATION pi{};
        BOOL ok = CreateProcessW(
            nullptr, wcmd.data(), nullptr, nullptr, FALSE,
            EXTENDED_STARTUPINFO_PRESENT, nullptr,
            wdir.empty() ? nullptr : wdir.data(),
            &si.StartupInfo, &pi);

        DeleteProcThreadAttributeList(lpAttr);
        if (!ok) return false;

        proc_    = pi.hProcess;
        pid_     = pi.dwProcessId;
        running_ = true;
        CloseHandle(pi.hThread);
        return true;
    }

    void write(std::string_view data) override {
        if (write_pipe_ == INVALID_HANDLE_VALUE) return;
        DWORD written;
        WriteFile(write_pipe_, data.data(), static_cast<DWORD>(data.size()), &written, nullptr);
    }

    void resize(int cols, int rows) override {
        if (con_) {
            COORD size{ static_cast<SHORT>(cols), static_cast<SHORT>(rows) };
            ResizePseudoConsole(con_, size);
        }
    }

    void kill() override {
        if (proc_ != INVALID_HANDLE_VALUE) {
            TerminateProcess(proc_, 0);
            CloseHandle(proc_);
            proc_ = INVALID_HANDLE_VALUE;
        }
        if (con_) { ClosePseudoConsole(con_); con_ = nullptr; }
        if (write_pipe_ != INVALID_HANDLE_VALUE) {
            CloseHandle(write_pipe_); write_pipe_ = INVALID_HANDLE_VALUE;
        }
        if (read_pipe_ != INVALID_HANDLE_VALUE) {
            CloseHandle(read_pipe_); read_pipe_ = INVALID_HANDLE_VALUE;
        }
        running_ = false;
    }

    int  pid()     const override { return static_cast<int>(pid_); }
    bool running() const override { return running_; }

    std::string read(int timeout_ms = -1) override {
        if (read_pipe_ == INVALID_HANDLE_VALUE) return {};

        if (timeout_ms >= 0) {
            DWORD deadline = GetTickCount() + static_cast<DWORD>(timeout_ms);
            DWORD avail = 0;
            while (avail == 0) {
                if (!PeekNamedPipe(read_pipe_, nullptr, 0, nullptr, &avail, nullptr))
                    return {};
                if (avail > 0) break;
                if (GetTickCount() >= deadline) return {};
                Sleep(10);
            }
        }

        char buf[4096];
        DWORD nread = 0;
        if (!ReadFile(read_pipe_, buf, sizeof(buf), &nread, nullptr) || nread == 0) {
            running_ = false;
            return {};
        }
        return std::string(buf, nread);
    }
};

#endif // _WIN32

// On non-Windows this file is never compiled, so no need for a Unix fallback here.
// The factory is defined in pty_unix.cpp on Unix and here on Windows.
#ifdef _WIN32
std::unique_ptr<Pty> Pty::create() {
    return std::make_unique<PtyWin>();
}
#endif
