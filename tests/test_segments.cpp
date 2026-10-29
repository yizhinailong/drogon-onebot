/**
 * @file test_segments.cpp
 * @brief Check the segments that the `MessageSegment` functions build.
 *
 * Runs without the app.
 */
#include <cstdint>

#include <drogon/drogon_test.h>

#include <drogon_onebot/types/message.hpp>

namespace {

    constexpr std::int64_t LONG_USER_ID{ 1234567890123 }; ///< QQ number beyond 32 bits.
    constexpr std::int32_t MESSAGE_ID{ -2147000001 };     ///< Negative message ID, to check that the sign survives.

} // namespace

DROGON_TEST(TextKeepsTextAsIs) {
    auto const segment{ drogon_onebot::MessageSegment::Text("a [CQ:face,id=1] & b") };
    CHECK(segment.type == "text");
    CHECK(segment.data["text"].asString() == "a [CQ:face,id=1] & b");
    CHECK(segment.data.size() == 1);
}

DROGON_TEST(AtSendsQqAsString) {
    auto const one{ drogon_onebot::MessageSegment::At(LONG_USER_ID) };
    CHECK(one.type == "at");
    CHECK(one.data["qq"].isString());
    CHECK(one.data["qq"].asString() == "1234567890123");

    auto const all{ drogon_onebot::MessageSegment::AtAll() };
    CHECK(all.type == "at");
    CHECK(all.data["qq"].asString() == "all");
}

DROGON_TEST(ReplyAndFaceSendIdsAsStrings) {
    auto const reply{ drogon_onebot::MessageSegment::Reply(MESSAGE_ID) };
    CHECK(reply.type == "reply");
    CHECK(reply.data["id"].isString());
    CHECK(reply.data["id"].asString() == "-2147000001");

    auto const face{ drogon_onebot::MessageSegment::Face(178) };
    CHECK(face.type == "face");
    CHECK(face.data["id"].asString() == "178");
}

DROGON_TEST(ImagePassesFileThrough) {
    for (auto const* file : { "https://example.com/a.png", "file:///tmp/a.png", "base64://iVBORw0KGgo=" }) {
        auto const image{ drogon_onebot::MessageSegment::Image(file) };
        CHECK(image.type == "image");
        CHECK(image.data["file"].asString() == file);
        CHECK(image.data.size() == 1);
    }
}

DROGON_TEST(SegmentsComposeWithHandBuiltOnes) {
    auto image{ drogon_onebot::MessageSegment::Image("https://example.com/a.png") };
    image.data["summary"] = "[picture]";
    drogon_onebot::Message const message{
        drogon_onebot::MessageSegment::Reply(1),
        drogon_onebot::MessageSegment::Text("look"),
        image,
        { .type = "dice" },
    };
    REQUIRE(message.size() == 4);
    CHECK(message[0].type == "reply");
    CHECK(message[1].type == "text");
    CHECK(message[2].data["summary"].asString() == "[picture]");
    CHECK(message[3].type == "dice");
    CHECK(message[3].data.isObject());
    CHECK(message[3].data.empty());
}

auto main(int argc, char** argv) -> int {
    return drogon::test::run(argc, argv);
}
