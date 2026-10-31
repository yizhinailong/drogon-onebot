/**
 * @file napcat.cpp
 * @brief Implement typed NapCat favourite-sticker extensions.
 */
#include <expected>
#include <string>
#include <utility>

#include "details/action_call.hpp"
#include "details/field_reader.hpp"
#include "drogon_onebot/onebot_client.hpp"

namespace {
    /**
     * @brief Read one favourite without dropping additional NapCat metadata.
     * @param data Favourite entry.
     * @return Typed favourite or the first malformed field.
     */
    auto read_face(Json::Value const& data) -> std::expected<drogon_onebot::CustomFace, std::string> {
        if (!data.isObject()) {
            return std::unexpected{ "entry must be an object" };
        }
        drogon_onebot::details::FieldReader fields{ data };
        auto const& id{ data.isMember("emojiId") ? data["emojiId"] : data["emoId"] };
        std::string emoji_id{};
        if (id.isString()) {
            emoji_id = id.asString();
        } else if (id.isUInt64()) {
            emoji_id = std::to_string(id.asUInt64());
        } else {
            return std::unexpected{ "emoId must be a string or nonnegative integer" };
        }
        drogon_onebot::CustomFace result{
            .emoji_id = std::move(emoji_id),
            .res_id = fields.RequiredString("resId"),
            .md5 = fields.String("md5"),
            .url = fields.String("url"),
            .description = fields.String("desc"),
            .raw = data,
        };
        if (!fields.Problem().empty()) {
            return std::unexpected{ fields.Problem() };
        }
        return result;
    }

    /**
     * @brief Read the favourite list returned by NapCat.
     * @param data Extension response data.
     * @return Typed favourites or a validation error.
     */
    auto read_faces(Json::Value const& data) -> std::expected<std::vector<drogon_onebot::CustomFace>, std::string> {
        return drogon_onebot::details::read_array<drogon_onebot::CustomFace>(data, "data", read_face);
    }
} // namespace

namespace drogon_onebot {
    /**
     * @brief Call NapCat's `fetch_custom_face_detail` extension.
     * @param params Request fields, read before this returns.
     * @return Task yielding the extension result, or why the action failed.
     */
    auto OneBot::FetchCustomFaceDetail(
        FetchCustomFaceDetailParams params
    ) const -> drogon::Task<Result<std::vector<CustomFace>>> {
        Json::Value body{ Json::objectValue };
        body["count"] = params.count;
        return details::call_and_read<std::vector<CustomFace>>(m_transport, "fetch_custom_face_detail", std::move(body), params.timeout, read_faces);
    }

    /**
     * @brief Call NapCat's `add_custom_face` extension.
     * @param params Request fields, read before this returns.
     * @return Task yielding the extension result, or why the action failed.
     */
    auto OneBot::AddCustomFace(
        AddCustomFaceParams params
    ) const -> drogon::Task<Result<Json::Value>> {
        Json::Value body{ Json::objectValue };
        body["file"] = params.file;
        if (params.emoji_id) {
            body["emoji_id"] = *params.emoji_id;
        }
        if (params.package_id) {
            body["package_id"] = *params.package_id;
        }
        if (params.file_name) {
            body["file_name"] = *params.file_name;
        }
        if (params.file_size) {
            body["file_size"] = *params.file_size;
        }
        if (params.md5) {
            body["md5"] = *params.md5;
        }
        if (params.is_mark_face) {
            body["is_mark_face"] = *params.is_mark_face;
        }
        if (params.is_origin) {
            body["is_origin"] = *params.is_origin;
        }
        return details::call(m_transport, "add_custom_face", std::move(body), params.timeout);
    }

    /**
     * @brief Call NapCat's `delete_custom_face` extension.
     * @param params Request fields, read before this returns.
     * @return Task yielding the extension result, or why the action failed.
     */
    auto OneBot::DeleteCustomFace(
        DeleteCustomFaceParams params
    ) const -> drogon::Task<Result<Json::Value>> {
        Json::Value body{ Json::objectValue };
        body["ids"] = Json::Value{ Json::arrayValue };
        for (auto const& id : params.res_ids) {
            body["ids"].append(id);
        }
        return details::call(m_transport, "delete_custom_face", std::move(body), params.timeout);
    }

    /**
     * @brief Call NapCat's `set_custom_face_desc` extension.
     * @param params Request fields, read before this returns.
     * @return Task yielding the extension result, or why the action failed.
     */
    auto OneBot::SetCustomFaceDesc(
        SetCustomFaceDescParams params
    ) const -> drogon::Task<Result<Json::Value>> {
        Json::Value body{ Json::objectValue };
        body["emoji_id"] = params.emoji_id;
        body["res_id"] = params.res_id;
        body["md5"] = params.md5;
        body["desc"] = params.description;
        return details::call(m_transport, "set_custom_face_desc", std::move(body), params.timeout);
    }

} // namespace drogon_onebot
