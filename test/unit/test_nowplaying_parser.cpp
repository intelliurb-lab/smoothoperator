#include <gtest/gtest.h>
#include "smoothoperator/nowplaying_parser.hpp"

using smoothoperator::drivers::parse_nowplaying;

TEST(NowPlayingParserTest, ParsesArtistAndTitle) {
    auto m = parse_nowplaying("Daft Punk|Technologic\r\nEND\r\n");
    EXPECT_EQ(m["artist"], "Daft Punk");
    EXPECT_EQ(m["title"], "Technologic");
    EXPECT_EQ(m["duration"], 0.0);
    EXPECT_EQ(m["playlist"], "default");
}

TEST(NowPlayingParserTest, LineWithoutPipeIsTitle) {
    auto m = parse_nowplaying("Station Jingle\r\nEND\r\n");
    EXPECT_EQ(m["title"], "Station Jingle");
    EXPECT_EQ(m["artist"], "Unknown");
}

TEST(NowPlayingParserTest, SplitsOnFirstPipeOnly) {
    auto m = parse_nowplaying("AC|DC|Song|Part 2\nEND\n");
    EXPECT_EQ(m["artist"], "AC");
    EXPECT_EQ(m["title"], "DC|Song|Part 2");
}

TEST(NowPlayingParserTest, EmptyOrEndOnlyGivesDefaults) {
    auto m = parse_nowplaying("END\r\n");
    EXPECT_EQ(m["title"], "Unknown");
    EXPECT_EQ(m["artist"], "Unknown");
}
