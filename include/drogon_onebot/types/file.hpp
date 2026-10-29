/**
 * @file file.hpp
 * @brief Declare the request and response types of the file actions.
 */
#pragma once

#include <string>

#include <json/value.h>

namespace drogon_onebot {

    /**
     * @brief Arguments of `OneBot::GetRecord()`.
     */
    struct GetRecordParams {
        std::string file;       ///< `file` of a received `record` segment.
        std::string out_format; ///< Format to convert to: `mp3`, `amr`, `wma`, `m4a`, `spx`, `ogg`, `wav`, or `flac`. Always sent; NapCat skips the conversion when it is empty.
        double timeout{ 30.0 }; ///< Seconds to wait for the response; 0 waits forever.
    };

    /**
     * @brief Arguments of `OneBot::GetImage()`.
     */
    struct GetImageParams {
        std::string file;       ///< `file` of a received `image` segment.
        double timeout{ 30.0 }; ///< Seconds to wait for the response; 0 waits forever.
    };

    /**
     * @brief File that the OneBot implementation fetched, from `get_record` or `get_image`.
     */
    struct FileInfo {
        std::string file; ///< Path of the file on the implementation's host, e.g. inside NapCat's container.
        Json::Value raw;  ///< Whole `data` as received, for fields without a member here, e.g. NapCat's `url`, `file_size`, and `base64`.
    };

} // namespace drogon_onebot
