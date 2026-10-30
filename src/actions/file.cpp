/**
 * @file file.cpp
 * @brief Implement the file actions of `OneBot`.
 */
#include <expected>
#include <string>
#include <utility>

#include "details/action_call.hpp"
#include "details/field_reader.hpp"
#include "drogon_onebot/onebot_client.hpp"

namespace {

    /**
     * @brief Read the `data` of a file action.
     * @param data Object to read.
     * @return The file, or the first problem found, e.g. `file is missing`.
     */
    auto to_file_info(Json::Value const& data) -> std::expected<drogon_onebot::FileInfo, std::string> {
        drogon_onebot::details::FieldReader fields{ data };
        drogon_onebot::FileInfo info{
            .file = fields.RequiredString("file"),
            .raw = data,
        };
        if (!fields.Problem().empty()) {
            return std::unexpected{ fields.Problem() };
        }
        return info;
    }

} // namespace

namespace drogon_onebot {

    /**
     * @brief Call `get_record`.
     * @param params Request fields, read before this returns.
     * @return Task yielding the converted voice file, or why the action failed.
     * @note NapCat converts the file with ffmpeg next to the original and also answers with
     * its content as `base64`, which `FileInfo::raw` keeps. It finds `file` among the media
     * of the messages it has received, in memory for a day, or else by name through QQ's
     * file assistant, and fails with `ErrorKind::Action` when neither has it.
     */
    auto OneBot::GetRecord(
        GetRecordParams params
    ) const -> drogon::Task<Result<FileInfo>> {
        Json::Value body{ Json::objectValue };
        body["file"] = params.file;
        // NapCat fails the call with retcode 400 without out_format, and skips the conversion when it is empty.
        body["out_format"] = params.out_format;
        return details::call_and_read<FileInfo>(m_transport, "get_record", std::move(body), params.timeout, to_file_info);
    }

    /**
     * @brief Call `get_image`.
     * @param params Request fields, read before this returns.
     * @return Task yielding the downloaded image file, or why the action failed.
     * @note NapCat answers with the content as `base64` only when its `enableLocalFile2Url`
     * setting is on. It finds `file` as `GetRecord()` describes.
     */
    auto OneBot::GetImage(
        GetImageParams params
    ) const -> drogon::Task<Result<FileInfo>> {
        Json::Value body{ Json::objectValue };
        body["file"] = params.file;
        return details::call_and_read<FileInfo>(m_transport, "get_image", std::move(body), params.timeout, to_file_info);
    }

} // namespace drogon_onebot
