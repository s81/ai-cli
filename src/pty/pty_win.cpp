#include "pty.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <vector>

#include <atomic>
#include <cwchar>
#include <filesystem>
#include <string>
#include <system_error>

namespace {

std::wstring utf8_to_wide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) return {};
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

// Finds `name` (".exe" appended if missing) in the absolute PATH entries only.
// Letting CreateProcess search instead would try ai-cli's own directory and
// the current directory first, so a claude.exe planted in whatever folder
// ai-cli is launched from (a freshly cloned repo, Downloads...) would run.
std::wstring find_on_path(const std::wstring& name) {
    namespace fs = std::filesystem;
    if (name.find_first_of(L"\\/:") != std::wstring::npos) return {}; // bare names only

    std::wstring exe = name;
    if (exe.size() < 4 || _wcsicmp(exe.c_str() + exe.size() - 4, L".exe") != 0)
        exe += L".exe";

    DWORD len = GetEnvironmentVariableW(L"PATH", nullptr, 0);
    if (len == 0) return {};
    std::wstring path(len, L'\0');
    len = GetEnvironmentVariableW(L"PATH", path.data(), len);
    path.resize(len);

    size_t start = 0;
    while (start <= path.size()) {
        size_t end = path.find(L';', start);
        if (end == std::wstring::npos) end = path.size();
        std::wstring dir = path.substr(start, end - start);
        start = end + 1;

        if (dir.size() >= 2 && dir.front() == L'"' && dir.back() == L'"')
            dir = dir.substr(1, dir.size() - 2);
        if (dir.empty()) continue;
        fs::path d(dir);
        if (!d.is_absolute()) continue; // ".", "bin", ... resolve against cwd

        std::error_code ec;
        fs::path candidate = d / exe;
        if (fs::is_regular_file(candidate, ec)) return candidate.wstring();
    }
    return {};
}

} // namespace

class PtyWin final : public Pty {
    HPCON  con_        = nullptr;
    HANDLE read_pipe_  = INVALID_HANDLE_VALUE;
    HANDLE write_pipe_ = INVALID_HANDLE_VALUE;
    HANDLE proc_       = INVALID_HANDLE_VALUE;
    HANDLE job_        = nullptr; // kills the whole process tree with the session
    DWORD  pid_        = 0;
    std::atomic<bool> running_{false};

public:
    ~PtyWin() override { kill(); }

    bool spawn(const std::string& cmd, const std::string& dir,
               int cols, int rows) override {
        std::wstring app = find_on_path(utf8_to_wide(cmd));
        if (app.empty()) return false;

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

        // Everything claude starts (node, MCP servers, tool processes) joins
        // this job and dies with the session, even if ai-cli itself crashes.
        job_ = CreateJobObjectW(nullptr, nullptr);
        if (job_) {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
            info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            SetInformationJobObject(job_, JobObjectExtendedLimitInformation,
                                    &info, sizeof(info));
        }

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

        // Full path as the application name; quoted path as argv[0].
        std::wstring wcmd = L"\"" + app + L"\"";
        std::wstring wdir = utf8_to_wide(dir);

        PROCESS_INFORMATION pi{};
        BOOL ok = CreateProcessW(
            app.c_str(), wcmd.data(), nullptr, nullptr,
            FALSE, // inherit no handles
            EXTENDED_STARTUPINFO_PRESENT | CREATE_SUSPENDED, nullptr,
            wdir.empty() ? nullptr : wdir.c_str(),
            &si.StartupInfo, &pi);

        DeleteProcThreadAttributeList(lpAttr);
        if (!ok) return false;

        // Assign before the first instruction runs so no child escapes the job.
        if (job_) AssignProcessToJobObject(job_, pi.hProcess);
        ResumeThread(pi.hThread);

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
        if (job_) {
            TerminateJobObject(job_, 1); // the whole tree, not just claude.exe
            CloseHandle(job_);
            job_ = nullptr;
        }
        if (proc_ != INVALID_HANDLE_VALUE) {
            TerminateProcess(proc_, 0);
            CloseHandle(proc_);
            proc_ = INVALID_HANDLE_VALUE;
        }
        // Close our pipe ends before the pseudoconsole: ClosePseudoConsole can
        // block while conhost still has output queued for a reader.
        if (write_pipe_ != INVALID_HANDLE_VALUE) {
            CloseHandle(write_pipe_); write_pipe_ = INVALID_HANDLE_VALUE;
        }
        if (read_pipe_ != INVALID_HANDLE_VALUE) {
            CloseHandle(read_pipe_); read_pipe_ = INVALID_HANDLE_VALUE;
        }
        if (con_) { ClosePseudoConsole(con_); con_ = nullptr; }
        running_ = false;
    }

    int  pid()     const override { return static_cast<int>(pid_); }

    // ConPTY never signals EOF on the output pipe when the child exits (the
    // pseudoconsole keeps it open), so ask the process handle directly.
    bool running() const override {
        if (!running_) return false;
        return proc_ != INVALID_HANDLE_VALUE &&
               WaitForSingleObject(proc_, 0) == WAIT_TIMEOUT;
    }

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
