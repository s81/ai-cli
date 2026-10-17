#include <gtest/gtest.h>
#include "session_store.h"
#include <filesystem>
#include <fstream>
#include <iterator>

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
    EXPECT_EQ(loaded[0]->state.load(), SessionState::Dead);
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

// ── Security: the store file is untrusted input. ───────────────────────

TEST_F(SessionStoreTest, LoadSkipsMalformedEntriesInsteadOfCrashing) {
    {
        std::ofstream f(path_);
        f << R"({"sessions":[1, "x", null, {"name":5}, {"id":[]},)"
             R"( {"id":"good","name":"ok"}]})";
    }
    auto v = SessionStore::load(path_);
    ASSERT_EQ(v.size(), 1u);
    EXPECT_EQ(v[0]->id, "good");
    EXPECT_EQ(v[0]->name, "ok");
}

TEST_F(SessionStoreTest, LoadRejectsNonObjectRoot) {
    { std::ofstream f(path_); f << "[1,2,3]"; }
    EXPECT_TRUE(SessionStore::load(path_).empty());
}

TEST_F(SessionStoreTest, LoadSanitizesSessionNames) {
    { std::ofstream f(path_); f << R"({"sessions":[{"name":"a\u001b]0;pwn\u0007b"}]})"; }
    auto v = SessionStore::load(path_);
    ASSERT_EQ(v.size(), 1u);
    EXPECT_EQ(v[0]->name.find('\x1b'), std::string::npos);
    EXPECT_EQ(v[0]->name.find('\x07'), std::string::npos);
}

TEST_F(SessionStoreTest, SaveSurvivesInvalidUtf8) {
    std::vector<std::unique_ptr<Session>> sessions;
    sessions.push_back(std::make_unique<Session>());
    sessions[0]->dir = "bad\xFF\xFE";
    EXPECT_NO_THROW(SessionStore::save(path_, sessions));
    EXPECT_EQ(SessionStore::load(path_).size(), 1u);
}

#ifndef _WIN32
TEST_F(SessionStoreTest, SavedFileIsPrivateToOwner) {
    std::vector<std::unique_ptr<Session>> empty;
    SessionStore::save(path_, empty);
    auto p = fs::status(path_).permissions();
    EXPECT_EQ(p & (fs::perms::group_all | fs::perms::others_all), fs::perms::none);
}

TEST_F(SessionStoreTest, SaveDoesNotFollowPlantedTmpSymlink) {
    fs::path victim = fs::temp_directory_path() / "ai-cli-test-victim.txt";
    { std::ofstream f(victim); f << "original"; }
    fs::path tmp = path_; tmp += ".tmp";
    fs::remove(tmp);
    fs::create_symlink(victim, tmp);

    std::vector<std::unique_ptr<Session>> empty;
    SessionStore::save(path_, empty);

    std::ifstream f(victim);
    std::string contents((std::istreambuf_iterator<char>(f)), {});
    EXPECT_EQ(contents, "original");
    fs::remove(victim);
    fs::remove(tmp);
}
#endif
