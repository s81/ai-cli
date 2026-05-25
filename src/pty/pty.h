#pragma once
#include <memory>
#include <string>
#include <string_view>

class Pty {
public:
    virtual ~Pty() = default;

    // Spawn a child process in a new PTY. Returns false on failure.
    virtual bool spawn(const std::string& command,
                       const std::string& dir,
                       int cols, int rows) = 0;

    virtual void write(std::string_view data) = 0;
    virtual void resize(int cols, int rows) = 0;
    virtual void kill() = 0;

    virtual int  pid() const = 0;
    virtual bool running() const = 0;

    // Read available data. timeout_ms: -1 = block, 0 = non-blocking, N = wait up to N ms.
    // Returns empty string on timeout, EOF, or error.
    virtual std::string read(int timeout_ms = -1) = 0;

    static std::unique_ptr<Pty> create();
};
