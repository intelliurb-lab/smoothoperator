#include <gtest/gtest.h>
#include "smoothoperator/nowplaying_parser.hpp"

using smoothoperator::drivers::parse_nowplaying;

TEST(NowPlayingParserTest, ParsesArtistAndTitle) {
    auto m = parse_nowplaying("Daft Punk|Technologic\r\nEND\r\n");
    EXPECT_EQ(m["artist"], "Daft Punk");
    EXPECT_EQ(m["title"], "Technologic");
    EXPECT_DOUBLE_EQ(m["duration"].get<double>(), 0.0);
    EXPECT_EQ(m["playlist"], "default");
}

TEST(NowPlayingParserTest, LineWithoutPipeIsTitle) {
    auto m = parse_nowplaying("Station Jingle\r\nEND\r\n");
    EXPECT_EQ(m["title"], "Station Jingle");
    EXPECT_EQ(m["artist"], "Unknown");
}

TEST(NowPlayingParserTest, ParsesValidDuration) {
    auto m = parse_nowplaying("Daft Punk|Technologic|284.5\r\nEND\r\n");
    EXPECT_EQ(m["artist"], "Daft Punk");
    EXPECT_EQ(m["title"], "Technologic");
    EXPECT_DOUBLE_EQ(m["duration"].get<double>(), 284.5);
}

TEST(NowPlayingParserTest, EmptyDurationIsZero) {
    auto m = parse_nowplaying("Daft Punk|Technologic|\nEND\n");
    EXPECT_EQ(m["title"], "Technologic");
    EXPECT_DOUBLE_EQ(m["duration"].get<double>(), 0.0);
}

TEST(NowPlayingParserTest, InvalidDurationIsZero) {
    for (const char* in : {"A|B|abc\n", "A|B|12abc\n", "A|B|-5\n", "A|B|nan\n"}) {
        auto m = parse_nowplaying(in);
        EXPECT_EQ(m["title"], "B") << in;
        EXPECT_DOUBLE_EQ(m["duration"].get<double>(), 0.0) << in;
    }
}

TEST(NowPlayingParserTest, TitleWithPipeAndDuration) {
    auto m = parse_nowplaying("AC|DC|Song|200\nEND\n");
    EXPECT_EQ(m["artist"], "AC");
    EXPECT_EQ(m["title"], "DC|Song");
    EXPECT_DOUBLE_EQ(m["duration"].get<double>(), 200.0);
}

TEST(NowPlayingParserTest, EmptyOrEndOnlyGivesDefaults) {
    auto m = parse_nowplaying("END\r\n");
    EXPECT_EQ(m["title"], "Unknown");
    EXPECT_EQ(m["artist"], "Unknown");
}
