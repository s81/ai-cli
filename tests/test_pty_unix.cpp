#include <gtest/gtest.h>
#include "pty/pty.h"
#include <chrono>
#include <string>

TEST(PtyUnix, SpawnAndReadOutput) {
    auto pty = Pty::create();
    ASSERT_NE(pty, nullptr);
    ASSERT_TRUE(pty->spawn("sh", "", 80, 24));
    EXPECT_TRUE(pty->running());
    EXPECT_GT(pty->pid(), 0);

    pty->write("echo hello_pty_marker\n");

    std::string output;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        auto chunk = pty->read(100); // 100 ms timeout
        output += chunk;
        if (output.find("hello_pty_marker") != std::string::npos) break;
    }

    EXPECT_NE(output.find("hello_pty_marker"), std::string::npos);
    pty->kill();
    EXPECT_FALSE(pty->running());
}

TEST(PtyUnix, ResizeDoesNotCrash) {
    auto pty = Pty::create();
    ASSERT_NE(pty, nullptr);
    ASSERT_TRUE(pty->spawn("sh", "", 80, 24));
    pty->resize(120, 40);
    pty->kill();
}

TEST(PtyUnix, KillIsIdempotent) {
    auto pty = Pty::create();
    ASSERT_NE(pty, nullptr);
    ASSERT_TRUE(pty->spawn("sh", "", 80, 24));
    pty->kill();
    pty->kill(); // must not crash or assert
    EXPECT_FALSE(pty->running());
}
