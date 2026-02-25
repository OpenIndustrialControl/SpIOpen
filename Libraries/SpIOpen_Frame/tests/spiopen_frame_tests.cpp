#include <gtest/gtest.h>
#include <spiopen_frame.h>

TEST(SpIOpenFrame, Constructor) {
    SpIOpenFrame frame;
    EXPECT_EQ(frame.GetFrameLength(), 0);
}

