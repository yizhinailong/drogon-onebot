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

DROGON_TEST(RecordPassesFileThrough) {
    for (auto const* file : { "https://example.com/a.amr", "file:///tmp/a.mp3", "8C1B4E7B.amr" }) {
        auto const record{ drogon_onebot::MessageSegment::Record(file) };
        CHECK(record.type == "record");
        CHECK(record.data["file"].asString() == file);
        CHECK(record.data.size() == 1);
    }
}

DROGON_TEST(VideoPassesFileThrough) {
    for (auto const* file : { "https://example.com/a.mp4", "/tmp/a.mp4", "base64://AAAAIGZ0eXA=" }) {
        auto const video{ drogon_onebot::MessageSegment::Video(file) };
        CHECK(video.type == "video");
        CHECK(video.data["file"].asString() == file);
        CHECK(video.data.size() == 1);
    }
}

DROGON_TEST(ContactSendsTypeAndIdAsString) {
    auto const user{ drogon_onebot::MessageSegment::ContactFriend(LONG_USER_ID) };
    CHECK(user.type == "contact");
    CHECK(user.data["type"].asString() == "qq");
    CHECK(user.data["id"].isString());
    CHECK(user.data["id"].asString() == "1234567890123");
    CHECK(user.data.size() == 2);

    auto const group{ drogon_onebot::MessageSegment::ContactGroup(30003) };
    CHECK(group.type == "contact");
    CHECK(group.data["type"].asString() == "group");
    CHECK(group.data["id"].asString() == "30003");
    CHECK(group.data.size() == 2);
}

DROGON_TEST(JsonSendsDataAsString) {
    auto const* const content{ R"({"app":"com.tencent.structmsg","meta":{"news":{"title":"hi"}}})" };
    auto const card{ drogon_onebot::MessageSegment::Json(content) };
    CHECK(card.type == "json");
    CHECK(card.data["data"].isString());
    CHECK(card.data["data"].asString() == content);
    CHECK(card.data.size() == 1);
}

DROGON_TEST(MusicSendsTypeAndId) {
    auto const song{ drogon_onebot::MessageSegment::Music("163", "28949129") };
    CHECK(song.type == "music");
    CHECK(song.data["type"].asString() == "163");
    CHECK(song.data["id"].isString());
    CHECK(song.data["id"].asString() == "28949129");
    CHECK(song.data.size() == 2);
}

DROGON_TEST(RpsAndDiceSendEmptyResult) {
    auto const rps{ drogon_onebot::MessageSegment::Rps() };
    CHECK(rps.type == "rps");
    CHECK(rps.data["result"].isString());
    CHECK(rps.data["result"].asString().empty());
    CHECK(rps.data.size() == 1);

    auto const dice{ drogon_onebot::MessageSegment::Dice() };
    CHECK(dice.type == "dice");
    CHECK(dice.data["result"].isString());
    CHECK(dice.data["result"].asString().empty());
    CHECK(dice.data.size() == 1);
}

DROGON_TEST(NodeReferencesMessageWithNapCatKeys) {
    auto const node{ drogon_onebot::MessageSegment::Node(MESSAGE_ID) };
    CHECK(node.type == "node");
    CHECK(node.data["id"].isString());
    CHECK(node.data["id"].asString() == "-2147000001");
    CHECK(node.data["nickname"].isString());
    CHECK(node.data["nickname"].asString().empty());
    CHECK(node.data.isMember("content"));
    CHECK(node.data["content"].isNull());
    CHECK(node.data.size() == 3);
}

DROGON_TEST(SegmentsComposeWithHandBuiltOnes) {
    auto image{ drogon_onebot::MessageSegment::Image("https://example.com/a.png") };
    image.data["summary"] = "[picture]";
    drogon_onebot::Message const message{
        drogon_onebot::MessageSegment::Reply(1),
        drogon_onebot::MessageSegment::Text("look"),
        image,
        { .type = "shake" },
    };
    REQUIRE(message.size() == 4);
    CHECK(message[0].type == "reply");
    CHECK(message[1].type == "text");
    CHECK(message[2].data["summary"].asString() == "[picture]");
    CHECK(message[3].type == "shake");
    CHECK(message[3].data.isObject());
    CHECK(message[3].data.empty());
}

auto main(int argc, char** argv) -> int {
    return drogon::test::run(argc, argv);
}
