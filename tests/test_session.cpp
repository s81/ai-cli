#include <gtest/gtest.h>
#include "session.h"

TEST(Session, AppendOutputSplitsOnNewlines) {
    Session s;
    s.append_output("hello\nworld\n");
    ASSERT_EQ(s.lines.size(), 2u);
    EXPECT_EQ(s.lines[0], "hello");
    EXPECT_EQ(s.lines[1], "world");
}

TEST(Session, AppendOutputStripsAnsiCsiSequences) {
    Session s;
    s.append_output("\x1b[31mred\x1b[0m\n");
    ASSERT_EQ(s.lines.size(), 1u);
    EXPECT_EQ(s.lines[0], "red");
}

TEST(Session, AppendOutputStripsAnsiSingleCharEscape) {
    Session s;
    s.append_output("\x1b" "Msome text\n");
    ASSERT_EQ(s.lines.size(), 1u);
    EXPECT_EQ(s.lines[0], "some text");
}

TEST(Session, AppendOutputNormalizesCarriageReturnNewline) {
    Session s;
    s.append_output("line1\r\nline2\r\n");
    ASSERT_EQ(s.lines.size(), 2u);
    EXPECT_EQ(s.lines[0], "line1");
    EXPECT_EQ(s.lines[1], "line2");
}

TEST(Session, RingBufferCapsAtMaxLines) {
    Session s;
    for (size_t i = 0; i < Session::MAX_LINES + 10; ++i) {
        s.append_output("line\n");
    }
    EXPECT_EQ(s.lines.size(), Session::MAX_LINES);
}

TEST(Session, MultipleAppendCallsMergeCorrectly) {
    Session s;
    s.append_output("hel");
    s.append_output("lo\nworld\n");
    ASSERT_EQ(s.lines.size(), 2u);
    EXPECT_EQ(s.lines[0], "hello");
    EXPECT_EQ(s.lines[1], "world");
}

// ── Security: program output must never reach the outer terminal as a
//    control sequence, and must not grow memory without bound. ──────────

static bool has_control_bytes(const std::string& s) {
    for (unsigned char c : s)
        if ((c < 0x20 && c != '\t') || c == 0x7F) return true;
    return false;
}

TEST(SessionSecurity, StripsOscTitleSequence) {
    Session s;
    s.append_output("\x1b]0;evil title\x07visible\n");
    ASSERT_EQ(s.lines.size(), 1u);
    EXPECT_EQ(s.lines[0], "visible");
}

TEST(SessionSecurity, StripsOscTerminatedByStringTerminator) {
    Session s;
    s.append_output("\x1b]52;c;ZXZpbA==\x1b\after\n"); // OSC 52 clipboard write
    ASSERT_EQ(s.lines.size(), 1u);
    EXPECT_EQ(s.lines[0], "after");
}

TEST(SessionSecurity, StripsDcsSequence) {
    Session s;
    s.append_output("\x1bPq#0;2;0;0;0\x1b\ok\n");
    ASSERT_EQ(s.lines.size(), 1u);
    EXPECT_EQ(s.lines[0], "ok");
}

TEST(SessionSecurity, CsiEndsOnNonLetterFinalByte) {
    Session s;
    s.append_output("\x1b[200~pasted\x1b[201~\n"); // bracketed paste markers
    ASSERT_EQ(s.lines.size(), 1u);
    EXPECT_EQ(s.lines[0], "pasted");
}

TEST(SessionSecurity, StripsCharsetDesignation) {
    Session s;
    s.append_output("\x1b(Btext\n");
    ASSERT_EQ(s.lines.size(), 1u);
    EXPECT_EQ(s.lines[0], "text");
}

TEST(SessionSecurity, EscapeSplitAcrossReadsIsStillStripped) {
    Session s;
    s.append_output("a\x1b");
    s.append_output("]0;ti");
    s.append_output("tle\x07");
    s.append_output("b\n");
    ASSERT_EQ(s.lines.size(), 1u);
    EXPECT_EQ(s.lines[0], "ab");
}

TEST(SessionSecurity, DropsUtf8EncodedC1Controls) {
    Session s;
    s.append_output("x\xC2\x9B" "31my\n"); // U+009B is an 8-bit CSI
    ASSERT_EQ(s.lines.size(), 1u);
    EXPECT_EQ(s.lines[0].find("\xC2\x9B"), std::string::npos);
}

TEST(SessionSecurity, KeepsOrdinaryUtf8) {
    Session s;
    s.append_output("caf\xC3\xA9 \xC2\xA9 \xE2\x9C\x93\n"); // é © ✓
    ASSERT_EQ(s.lines.size(), 1u);
    EXPECT_EQ(s.lines[0], "caf\xC3\xA9 \xC2\xA9 \xE2\x9C\x93");
}

TEST(SessionSecurity, DropsOtherControlBytes) {
    Session s;
    s.append_output(std::string("a\x07" "b\x08" "c\x7F" "d\x00" "e\n", 10));
    ASSERT_EQ(s.lines.size(), 1u);
    EXPECT_EQ(s.lines[0], "abcde");
    EXPECT_FALSE(has_control_bytes(s.lines[0]));
}

TEST(SessionSecurity, UnterminatedOscDoesNotHideOutputForever) {
    Session s;
    s.append_output("\x1b]0;");
    s.append_output(std::string(5000, 'x'));
    s.append_output("\nlater\n");
    ASSERT_FALSE(s.lines.empty());
    EXPECT_EQ(s.lines.back(), "later");
}

TEST(SessionSecurity, VeryLongLineIsSplitNotUnbounded) {
    Session s;
    s.append_output(std::string(Session::MAX_LINE_BYTES * 3, 'a'));
    EXPECT_EQ(s.lines.size(), 3u);
    for (const auto& l : s.lines) EXPECT_LE(l.size(), Session::MAX_LINE_BYTES);
}

TEST(SanitizeDisplayName, RemovesEscapeAndControlBytes) {
    EXPECT_EQ(sanitize_display_name("ok\x1b]0;pwn\x07name\n"), "ok]0;pwnname");
    EXPECT_EQ(sanitize_display_name("a\xC2\x9B" "b"), "ab");
}

TEST(SanitizeDisplayName, TruncatesOnUtf8Boundary) {
    std::string name = std::string(63, 'a') + "\xC3\xA9"; // 65 bytes, é straddles 64
    auto out = sanitize_display_name(name, 64);
    EXPECT_EQ(out, std::string(63, 'a'));
}
