#include "pty.h"

#if defined(__APPLE__)
#  include <util.h>
#else
#  include <pty.h>
#endif

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <cstring>

class PtyUnix final : public Pty {
    int   master_fd_ = -1;
    pid_t child_     = -1;
    std::atomic<bool> running_{false};

public:
    ~PtyUnix() override {
        kill();
        if (master_fd_ != -1) ::close(master_fd_);
    }

    bool spawn(const std::string& cmd, const std::string& dir,
               int cols, int rows) override {
        struct winsize ws{};
        ws.ws_col = static_cast<unsigned short>(cols);
        ws.ws_row = static_cast<unsigned short>(rows);

        child_ = forkpty(&master_fd_, nullptr, nullptr, &ws);
        if (child_ < 0) return false;

        if (child_ == 0) {
            if (!dir.empty()) ::chdir(dir.c_str());
            ::execlp(cmd.c_str(), cmd.c_str(), nullptr);
            ::_exit(127);
        }

        running_ = true;
        return true;
    }

    void write(std::string_view data) override {
        if (master_fd_ != -1)
            ::write(master_fd_, data.data(), data.size());
    }

    void resize(int cols, int rows) override {
        if (master_fd_ == -1) return;
        struct winsize ws{};
        ws.ws_col = static_cast<unsigned short>(cols);
        ws.ws_row = static_cast<unsigned short>(rows);
        ::ioctl(master_fd_, TIOCSWINSZ, &ws);
    }

    void kill() override {
        if (child_ > 0) {
            ::kill(child_, SIGTERM);
            ::waitpid(child_, nullptr, WNOHANG);
            child_ = -1;
        }
        running_ = false;
    }

    int  pid()     const override { return static_cast<int>(child_); }
    bool running() const override { return running_; }

    std::string read(int timeout_ms = -1) override {
        if (master_fd_ == -1) return {};

        struct pollfd pfd{ master_fd_, POLLIN, 0 };
        int ret = ::poll(&pfd, 1, timeout_ms);
        if (ret <= 0) return {};

        char buf[4096];
        ssize_t n = ::read(master_fd_, buf, sizeof(buf));
        if (n <= 0) { running_ = false; return {}; }
        return std::string(buf, static_cast<size_t>(n));
    }
};

std::unique_ptr<Pty> Pty::create() {
    return std::make_unique<PtyUnix>();
}
