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
