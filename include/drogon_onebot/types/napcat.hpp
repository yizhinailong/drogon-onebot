/**
 * @file napcat.hpp
 * @brief Declare typed NapCat favourite-sticker extension actions.
 */
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <json/value.h>

namespace drogon_onebot {

    /**
     * @brief Arguments of `OneBot::FetchCustomFaceDetail()`.
     */
    struct FetchCustomFaceDetailParams {
        std::int32_t count{ 48 }; ///< Maximum favourites requested from NapCat.
        double timeout{ 30.0 };   ///< Response timeout in seconds.
    };

    /**
     * @brief A favourite returned by NapCat's `fetch_custom_face_detail`.
     */
    struct CustomFace {
        std::string emoji_id;    ///< Numeric emoji identifier, normalized to text.
        std::string res_id;      ///< Resource identifier used for deletion.
        std::string md5;         ///< Content checksum.
        std::string url;         ///< Download URL, when supplied by NapCat.
        std::string description; ///< Favourite description.
        Json::Value raw;         ///< Complete entry, including implementation-specific fields.
    };

    /**
     * @brief Arguments of `OneBot::AddCustomFace()`.
     */
    struct AddCustomFaceParams {
        std::string file;                      ///< Existing path on the NapCat host, not an HTTP URL.
        std::optional<std::string> emoji_id;   ///< Optional emoji identifier.
        std::optional<std::string> package_id; ///< Optional package identifier.
        std::optional<std::string> file_name;  ///< Optional file name.
        std::optional<std::string> file_size;  ///< Optional size; NapCat still stats the file.
        std::optional<std::string> md5;        ///< Optional checksum; otherwise calculated by NapCat.
        std::optional<bool> is_mark_face;      ///< Whether this is a marketplace sticker.
        std::optional<bool> is_origin;         ///< Whether to preserve the original image.
        double timeout{ 30.0 };                ///< Response timeout in seconds.
    };

    /**
     * @brief Arguments of `OneBot::DeleteCustomFace()`.
     */
    struct DeleteCustomFaceParams {
        std::vector<std::string> res_ids; ///< Resource identifiers, sent as `ids`.
        double timeout{ 30.0 };           ///< Response timeout in seconds.
    };

    /**
     * @brief Arguments of `OneBot::SetCustomFaceDesc()`.
     */
    struct SetCustomFaceDescParams {
        std::string emoji_id;    ///< Numeric emoji identifier.
        std::string res_id;      ///< Resource identifier.
        std::string md5;         ///< Checksum of the favourite.
        std::string description; ///< New description, sent as `desc`.
        double timeout{ 30.0 };  ///< Response timeout in seconds.
    };

} // namespace drogon_onebot
