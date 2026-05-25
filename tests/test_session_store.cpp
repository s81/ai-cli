#include <gtest/gtest.h>
#include "session_store.h"
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

class SessionStoreTest : public ::testing::Test {
protected:
    fs::path path_;
    void SetUp() override {
        path_ = fs::temp_directory_path() / "ai-cli-test-sessions.json";
        fs::remove(path_);
    }
    void TearDown() override { fs::remove(path_); }
};

TEST_F(SessionStoreTest, LoadReturnsEmptyWhenFileMissing) {
    auto v = SessionStore::load(path_);
    EXPECT_TRUE(v.empty());
}

TEST_F(SessionStoreTest, SaveAndLoadRoundTrip) {
    Session s;
    s.id          = "abc-123";
    s.name        = "my-proj";
    s.dir         = "/home/user/proj";
    s.command     = "claude";
    s.created_at  = "2026-05-26T00:00:00Z";
    s.last_active = "2026-05-26T01:00:00Z";

    std::vector<std::unique_ptr<Session>> sessions;
    sessions.push_back(std::make_unique<Session>(std::move(s)));
    SessionStore::save(path_, sessions);

    auto loaded = SessionStore::load(path_);
    ASSERT_EQ(loaded.size(), 1u);
    EXPECT_EQ(loaded[0]->id,          "abc-123");
    EXPECT_EQ(loaded[0]->name,        "my-proj");
    EXPECT_EQ(loaded[0]->dir,         "/home/user/proj");
    EXPECT_EQ(loaded[0]->command,     "claude");
    EXPECT_EQ(loaded[0]->state,       SessionState::Dead);
    EXPECT_EQ(loaded[0]->created_at,  "2026-05-26T00:00:00Z");
    EXPECT_EQ(loaded[0]->last_active, "2026-05-26T01:00:00Z");
}

TEST_F(SessionStoreTest, LoadReturnsEmptyOnCorruptJson) {
    { std::ofstream f(path_); f << "{{not json}}"; }
    auto v = SessionStore::load(path_);
    EXPECT_TRUE(v.empty());
}

TEST_F(SessionStoreTest, SaveCreatesParentDirectories) {
    fs::path nested = fs::temp_directory_path() / "ai-cli-nested-xyz" / "sessions.json";
    fs::remove_all(nested.parent_path());
    std::vector<std::unique_ptr<Session>> empty;
    SessionStore::save(nested, empty);
    EXPECT_TRUE(fs::exists(nested));
    fs::remove_all(nested.parent_path());
}
