#include "nvidia_app_cleaner/driver_download_repair.h"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <system_error>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace nvidia_app_cleaner {
namespace {

constexpr std::uintmax_t kMaximumStatusFileSize = 1024U * 1024U;

struct JsonValue {
    enum class Type { string, number, other } type{Type::other};
    std::string text;
};

struct JsonObjectRecord {
    std::size_t start{0};
    std::size_t end{0};
    std::map<std::string, JsonValue> fields;
};

struct JsonDocument {
    bool valid{false};
    std::vector<JsonObjectRecord> records;
    std::string error;
};

class JsonReader {
  public:
    explicit JsonReader(const std::string &input) : input_(input) {}

    JsonDocument read_object_array() {
        JsonDocument document;
        skip_whitespace();
        if (!consume('[')) {
            document.error = "Status JSON must contain an array";
            return document;
        }
        skip_whitespace();
        if (consume(']')) {
            skip_whitespace();
            document.valid = position_ == input_.size();
            if (!document.valid) {
                document.error = "Unexpected data after the status array";
            }
            return document;
        }

        for (;;) {
            JsonObjectRecord record;
            record.start = position_;
            if (!read_object(record.fields, 0, document.error)) {
                return document;
            }
            record.end = position_;
            document.records.push_back(std::move(record));

            skip_whitespace();
            if (consume(']')) {
                break;
            }
            if (!consume(',')) {
                document.error = "Invalid array separator in status JSON";
                return document;
            }
            skip_whitespace();
        }
        skip_whitespace();
        if (position_ != input_.size()) {
            document.error = "Unexpected data after the status array";
            return document;
        }
        document.valid = true;
        return document;
    }

  private:
    bool read_object(std::map<std::string, JsonValue> &fields, int depth, std::string &error) {
        if (depth > 32 || !consume('{')) {
            error = "Invalid object in status JSON";
            return false;
        }
        skip_whitespace();
        if (consume('}')) {
            return true;
        }

        for (;;) {
            std::string key;
            if (!read_string(key)) {
                error = "Invalid property name in status JSON";
                return false;
            }
            skip_whitespace();
            if (!consume(':')) {
                error = "Missing property separator in status JSON";
                return false;
            }
            skip_whitespace();

            JsonValue value;
            if (peek() == '"') {
                value.type = JsonValue::Type::string;
                if (!read_string(value.text)) {
                    error = "Invalid string value in status JSON";
                    return false;
                }
            } else if (peek() == '-' || (peek() >= '0' && peek() <= '9')) {
                value.type = JsonValue::Type::number;
                if (!read_number(value.text)) {
                    error = "Invalid number in status JSON";
                    return false;
                }
            } else if (!skip_value(depth + 1)) {
                error = "Invalid value in status JSON";
                return false;
            }

            if (!fields.emplace(std::move(key), std::move(value)).second) {
                error = "Duplicate property in status JSON";
                return false;
            }

            skip_whitespace();
            if (consume('}')) {
                return true;
            }
            if (!consume(',')) {
                error = "Invalid object separator in status JSON";
                return false;
            }
            skip_whitespace();
        }
    }

    bool skip_value(int depth) {
        if (depth > 32) {
            return false;
        }
        if (peek() == '{') {
            std::map<std::string, JsonValue> ignored;
            std::string ignored_error;
            return read_object(ignored, depth, ignored_error);
        }
        if (peek() == '[') {
            ++position_;
            skip_whitespace();
            if (consume(']')) {
                return true;
            }
            for (;;) {
                if (!skip_value(depth + 1)) {
                    return false;
                }
                skip_whitespace();
                if (consume(']')) {
                    return true;
                }
                if (!consume(',')) {
                    return false;
                }
                skip_whitespace();
            }
        }
        if (peek() == '"') {
            std::string ignored;
            return read_string(ignored);
        }
        if (peek() == '-' || (peek() >= '0' && peek() <= '9')) {
            std::string ignored;
            return read_number(ignored);
        }
        return consume_literal("true") || consume_literal("false") || consume_literal("null");
    }

    bool read_string(std::string &output) {
        if (!consume('"')) {
            return false;
        }
        while (position_ < input_.size()) {
            const char character = input_[position_++];
            if (character == '"') {
                return true;
            }
            if (static_cast<unsigned char>(character) < 0x20U) {
                return false;
            }
            if (character != '\\') {
                output.push_back(character);
                continue;
            }
            if (position_ == input_.size()) {
                return false;
            }
            const char escaped = input_[position_++];
            switch (escaped) {
            case '"':
            case '\\':
            case '/':
                output.push_back(escaped);
                break;
            case 'b':
                output.push_back('\b');
                break;
            case 'f':
                output.push_back('\f');
                break;
            case 'n':
                output.push_back('\n');
                break;
            case 'r':
                output.push_back('\r');
                break;
            case 't':
                output.push_back('\t');
                break;
            default:
                return false;
            }
        }
        return false;
    }

    bool read_number(std::string &output) {
        const std::size_t start = position_;
        if (peek() == '-') {
            ++position_;
        }
        if (peek() == '0') {
            ++position_;
        } else {
            if (peek() < '1' || peek() > '9') {
                return false;
            }
            while (peek() >= '0' && peek() <= '9') {
                ++position_;
            }
        }
        if (peek() == '.') {
            ++position_;
            if (peek() < '0' || peek() > '9') {
                return false;
            }
            while (peek() >= '0' && peek() <= '9') {
                ++position_;
            }
        }
        if (peek() == 'e' || peek() == 'E') {
            ++position_;
            if (peek() == '+' || peek() == '-') {
                ++position_;
            }
            if (peek() < '0' || peek() > '9') {
                return false;
            }
            while (peek() >= '0' && peek() <= '9') {
                ++position_;
            }
        }
        output = input_.substr(start, position_ - start);
        return true;
    }

    void skip_whitespace() {
        while (position_ < input_.size() &&
               (input_[position_] == ' ' || input_[position_] == '\n' ||
                input_[position_] == '\r' || input_[position_] == '\t')) {
            ++position_;
        }
    }

    bool consume(char expected) {
        if (peek() != expected) {
            return false;
        }
        ++position_;
        return true;
    }

    bool consume_literal(const char *literal) {
        const std::size_t start = position_;
        while (*literal != '\0') {
            if (peek() != *literal) {
                position_ = start;
                return false;
            }
            ++position_;
            ++literal;
        }
        return true;
    }

    char peek() const { return position_ < input_.size() ? input_[position_] : '\0'; }

    const std::string &input_;
    std::size_t position_{0};
};

std::optional<std::string> read_small_file(const std::filesystem::path &path, std::string &error) {
    std::error_code file_error;
    const auto size = std::filesystem::file_size(path, file_error);
    if (file_error) {
        error = file_error.message();
        return std::nullopt;
    }
    if (size > kMaximumStatusFileSize) {
        error = "Status file is unexpectedly large";
        return std::nullopt;
    }

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "Status file could not be opened";
        return std::nullopt;
    }
    std::ostringstream content;
    content << input.rdbuf();
    if (!input.good() && !input.eof()) {
        error = "Status file could not be read completely";
        return std::nullopt;
    }
    return content.str();
}

const JsonValue *field(const JsonObjectRecord &record, const char *name, JsonValue::Type type) {
    const auto iterator = record.fields.find(name);
    if (iterator == record.fields.end() || iterator->second.type != type) {
        return nullptr;
    }
    return &iterator->second;
}

bool parse_integer(const JsonValue &value, int &result) {
    const char *begin = value.text.data();
    const char *end = begin + value.text.size();
    const auto conversion = std::from_chars(begin, end, result);
    return conversion.ec == std::errc{} && conversion.ptr == end;
}

bool parse_decimal(const JsonValue &value, double &result) {
    std::istringstream input(value.text);
    input.imbue(std::locale::classic());
    input >> result;
    return input && input.peek() == std::char_traits<char>::eof() && std::isfinite(result);
}

std::filesystem::path utf8_path(const std::string &value) {
    const std::u8string utf8(value.begin(), value.end());
    return std::filesystem::path(utf8);
}

bool remove_task_record(const std::string &content, const std::string &task_id, bool require_match,
                        std::string &rewritten, bool &removed, std::string &error) {
    const JsonDocument document = JsonReader(content).read_object_array();
    if (!document.valid) {
        error = document.error;
        return false;
    }

    std::size_t match_count = 0;
    for (const auto &record : document.records) {
        const JsonValue *record_task = field(record, "taskId", JsonValue::Type::string);
        if (record_task != nullptr && record_task->text == task_id) {
            ++match_count;
        }
    }
    if (match_count > 1) {
        error = "Multiple status records use the same taskId";
        return false;
    }
    if (match_count == 0) {
        if (require_match) {
            error = "The stale taskId was not found in the status file";
            return false;
        }
        rewritten = content;
        removed = false;
        return true;
    }

    rewritten = "[";
    bool first = true;
    for (const auto &record : document.records) {
        const JsonValue *record_task = field(record, "taskId", JsonValue::Type::string);
        if (record_task != nullptr && record_task->text == task_id) {
            continue;
        }
        if (!first) {
            rewritten += ',';
        }
        rewritten.append(content, record.start, record.end - record.start);
        first = false;
    }
    rewritten += "]\n";
    removed = true;
    return true;
}

bool replace_file_content(const std::filesystem::path &path, const std::string &content,
                          std::string &error) {
    auto temporary = path;
    temporary += L".nvidia-app-cleaner.tmp";
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);

    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output << content;
        output.flush();
        if (!output) {
            error = "Temporary status file could not be written";
            std::filesystem::remove(temporary, ignored);
            return false;
        }
    }

#ifdef _WIN32
    if (MoveFileExW(temporary.c_str(), path.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0) {
        error = std::system_category().message(static_cast<int>(GetLastError()));
        std::filesystem::remove(temporary, ignored);
        return false;
    }
#else
    std::filesystem::rename(temporary, path, ignored);
    if (ignored) {
        error = ignored.message();
        std::filesystem::remove(temporary, ignored);
        return false;
    }
#endif
    return true;
}

DriverDownloadAssessment assess_record(const JsonObjectRecord &record, DriverChannel channel) {
    DriverDownloadAssessment assessment;
    assessment.channel = channel;

    const JsonValue *state_value = field(record, "state", JsonValue::Type::number);
    const JsonValue *percent_value = field(record, "percentComplete", JsonValue::Type::number);
    const JsonValue *location_value = field(record, "fileLocation", JsonValue::Type::string);
    const JsonValue *task_value = field(record, "taskId", JsonValue::Type::string);
    const JsonValue *version_value = field(record, "version", JsonValue::Type::string);
    if (state_value == nullptr || percent_value == nullptr || location_value == nullptr ||
        task_value == nullptr) {
        assessment.state = DriverDownloadState::unsupported_record;
        assessment.detail = "Required download status fields are missing";
        return assessment;
    }

    int download_state = 0;
    double percent_complete = 0.0;
    if (!parse_integer(*state_value, download_state) ||
        !parse_decimal(*percent_value, percent_complete)) {
        assessment.state = DriverDownloadState::unsupported_record;
        assessment.detail = "Download state fields have unsupported values";
        return assessment;
    }
    assessment.task_id = task_value->text;
    if (assessment.task_id.empty()) {
        assessment.state = DriverDownloadState::unsupported_record;
        assessment.detail = "Download record has no taskId";
        return assessment;
    }
    if (version_value != nullptr) {
        assessment.version = version_value->text;
    }

    if (download_state != 8 || std::abs(percent_complete - 100.0) > 0.001) {
        assessment.state = DriverDownloadState::downloading;
        return assessment;
    }
    if (location_value->text.empty()) {
        assessment.state = DriverDownloadState::broken_file_location;
        assessment.detail = "Download is complete but fileLocation is empty";
        return assessment;
    }

    assessment.package_path = utf8_path(location_value->text);
    std::error_code path_error;
    const auto package_status =
        std::filesystem::symlink_status(assessment.package_path, path_error);
    if (path_error) {
        if (path_error == std::errc::no_such_file_or_directory) {
            assessment.state = DriverDownloadState::broken_file_location;
            assessment.detail = "Download is complete but the installer no longer exists";
        } else {
            assessment.state = DriverDownloadState::inaccessible;
            assessment.detail = path_error.message();
        }
        return assessment;
    }
    if (!std::filesystem::exists(package_status)) {
        assessment.state = DriverDownloadState::broken_file_location;
        assessment.detail = "Download is complete but the installer no longer exists";
        return assessment;
    }
    if (!std::filesystem::is_regular_file(package_status)) {
        assessment.state = DriverDownloadState::unsupported_record;
        assessment.detail = "fileLocation does not point to a normal file";
        return assessment;
    }

    assessment.state = DriverDownloadState::ready_to_install;
    return assessment;
}

std::filesystem::path status_directory(const std::filesystem::path &root, DriverChannel channel) {
    return root / L"status" / driver_channel_directory(channel);
}

DriverDownloadAssessment
assess_download_status(const std::filesystem::path &update_framework_root, DriverChannel channel,
                       std::optional<std::string_view> requested_task_id = std::nullopt) {
    DriverDownloadAssessment assessment;
    assessment.channel = channel;
    if (requested_task_id && requested_task_id->empty()) {
        assessment.state = DriverDownloadState::unsupported_record;
        assessment.detail = "The requested taskId is empty";
        return assessment;
    }

    const auto download_status =
        status_directory(update_framework_root, channel) / L"download.json";
    std::error_code path_error;
    const auto status = std::filesystem::symlink_status(download_status, path_error);
    if (path_error) {
        if (path_error == std::errc::no_such_file_or_directory) {
            assessment.state = DriverDownloadState::no_record;
        } else {
            assessment.state = DriverDownloadState::inaccessible;
            assessment.detail = path_error.message();
        }
        return assessment;
    }
    if (!std::filesystem::exists(status)) {
        assessment.state = DriverDownloadState::no_record;
        return assessment;
    }
    if (std::filesystem::is_symlink(status) || !std::filesystem::is_regular_file(status)) {
        assessment.state = DriverDownloadState::unsupported_record;
        assessment.detail = "Download status is not a normal file";
        return assessment;
    }

    std::string read_error;
    const auto content = read_small_file(download_status, read_error);
    if (!content) {
        assessment.state = DriverDownloadState::inaccessible;
        assessment.detail = std::move(read_error);
        return assessment;
    }
    const JsonDocument document = JsonReader(*content).read_object_array();
    if (!document.valid) {
        assessment.state = DriverDownloadState::unsupported_record;
        assessment.detail = document.error;
        return assessment;
    }
    if (document.records.empty()) {
        assessment.state = DriverDownloadState::no_record;
        return assessment;
    }

    if (requested_task_id) {
        const JsonObjectRecord *matched_record = nullptr;
        for (const auto &record : document.records) {
            const JsonValue *task_value = field(record, "taskId", JsonValue::Type::string);
            if (task_value == nullptr || task_value->text != *requested_task_id) {
                continue;
            }
            if (matched_record != nullptr) {
                assessment.state = DriverDownloadState::unsupported_record;
                assessment.detail = "Multiple status records use the requested taskId";
                return assessment;
            }
            matched_record = &record;
        }
        if (matched_record == nullptr) {
            assessment.state = DriverDownloadState::no_record;
            return assessment;
        }
        return assess_record(*matched_record, channel);
    }

    DriverDownloadAssessment fallback = assess_record(document.records.front(), channel);
    if (fallback.state == DriverDownloadState::broken_file_location) {
        return fallback;
    }
    for (std::size_t index = 1; index < document.records.size(); ++index) {
        auto record_assessment = assess_record(document.records[index], channel);
        if (record_assessment.state == DriverDownloadState::broken_file_location) {
            return record_assessment;
        }
        if (fallback.state == DriverDownloadState::unsupported_record ||
            fallback.state == DriverDownloadState::no_record) {
            fallback = std::move(record_assessment);
        }
    }
    return fallback;
}

DriverDownloadRepairResult
remove_download_status_record(const std::filesystem::path &update_framework_root,
                              const DriverDownloadAssessment &assessment,
                              const std::optional<std::filesystem::path> &backup_directory) {
    DriverDownloadRepairResult result;
    const auto channel_status = status_directory(update_framework_root, assessment.channel);
    const auto download_status = channel_status / L"download.json";
    const auto postprocessing_status = channel_status / L"postprocessing.json";

    std::string read_error;
    const auto download_content = read_small_file(download_status, read_error);
    if (!download_content) {
        result.detail = "Download status could not be read: " + read_error;
        return result;
    }
    std::string rewritten_download;
    bool download_record_removed = false;
    if (!remove_task_record(*download_content, assessment.task_id, true, rewritten_download,
                            download_record_removed, read_error)) {
        result.detail = "Download status could not be updated safely: " + read_error;
        return result;
    }

    std::error_code error;
    const auto postprocessing_file_status =
        std::filesystem::symlink_status(postprocessing_status, error);
    if (error) {
        if (error != std::errc::no_such_file_or_directory) {
            result.detail = "Post-processing status could not be inspected: " + error.message();
            return result;
        }
        error.clear();
    }
    const bool has_postprocessing = std::filesystem::exists(postprocessing_file_status);
    if (has_postprocessing && (std::filesystem::is_symlink(postprocessing_file_status) ||
                               !std::filesystem::is_regular_file(postprocessing_file_status))) {
        result.detail = "Post-processing status is not a normal file";
        return result;
    }

    std::string rewritten_postprocessing;
    bool postprocessing_record_removed = false;
    if (has_postprocessing) {
        const auto postprocessing_content = read_small_file(postprocessing_status, read_error);
        if (!postprocessing_content) {
            result.detail = "Post-processing status could not be read: " + read_error;
            return result;
        }
        if (!remove_task_record(*postprocessing_content, assessment.task_id, false,
                                rewritten_postprocessing, postprocessing_record_removed,
                                read_error)) {
            result.detail = "Post-processing status could not be updated safely: " + read_error;
            return result;
        }
    }

    if (backup_directory) {
        std::filesystem::create_directories(*backup_directory, error);
        if (error) {
            result.detail = "Backup directory could not be created: " + error.message();
            return result;
        }
        std::filesystem::copy_file(download_status, *backup_directory / L"download.json",
                                   std::filesystem::copy_options::none, error);
        if (error) {
            result.detail = "Download status could not be backed up: " + error.message();
            return result;
        }
        if (has_postprocessing) {
            std::filesystem::copy_file(postprocessing_status,
                                       *backup_directory / L"postprocessing.json",
                                       std::filesystem::copy_options::none, error);
            if (error) {
                result.detail = "Post-processing status could not be backed up: " + error.message();
                return result;
            }
        }
    }

    std::string replace_error;
    if (!download_record_removed ||
        !replace_file_content(download_status, rewritten_download, replace_error)) {
        result.detail = "Download status record could not be removed: " + replace_error;
        return result;
    }
    if (postprocessing_record_removed &&
        !replace_file_content(postprocessing_status, rewritten_postprocessing, replace_error)) {
        std::string rollback_error;
        const bool rolled_back =
            replace_file_content(download_status, *download_content, rollback_error);
        result.detail = "Post-processing status record could not be removed: " + replace_error;
        if (!rolled_back) {
            result.detail += "; download status rollback also failed: " + rollback_error;
        }
        return result;
    }

    result.repaired = true;
    if (backup_directory) {
        result.backup_directory = *backup_directory;
    }
    result.detail = "The completed-download record was removed";
    return result;
}

} // namespace

const wchar_t *driver_channel_directory(DriverChannel channel) {
    switch (channel) {
    case DriverChannel::game_ready:
        return L"grd";
    case DriverChannel::studio:
        return L"crd";
    case DriverChannel::nvidia_app:
        return L"nvapp";
    }
    return L"";
}

DriverDownloadAssessment
DriverDownloadRepair::assess(const std::filesystem::path &update_framework_root,
                             DriverChannel channel) const {
    return assess_download_status(update_framework_root, channel);
}

DriverDownloadAssessment
DriverDownloadRepair::assess_task(const std::filesystem::path &update_framework_root,
                                  DriverChannel channel, std::string_view task_id) const {
    return assess_download_status(update_framework_root, channel, task_id);
}

DriverDownloadRepairResult
DriverDownloadRepair::repair(const std::filesystem::path &update_framework_root,
                             DriverChannel channel,
                             const std::filesystem::path &backup_directory) const {
    const auto assessment = assess(update_framework_root, channel);
    if (assessment.state != DriverDownloadState::broken_file_location) {
        DriverDownloadRepairResult result;
        result.detail = "The download record is not a completed download with a missing installer";
        return result;
    }
    return remove_download_status_record(update_framework_root, assessment, backup_directory);
}

DriverDownloadRepairResult
DriverDownloadRepair::discard_completed_download(const std::filesystem::path &update_framework_root,
                                                 DriverChannel channel) const {
    const auto assessment = assess(update_framework_root, channel);
    if (assessment.state != DriverDownloadState::ready_to_install &&
        assessment.state != DriverDownloadState::broken_file_location) {
        DriverDownloadRepairResult result;
        result.detail = "The download record is not safe to discard";
        return result;
    }
    return remove_download_status_record(update_framework_root, assessment, std::nullopt);
}

DriverDownloadRepairResult DriverDownloadRepair::discard_completed_download_task(
    const std::filesystem::path &update_framework_root, DriverChannel channel,
    std::string_view task_id) const {
    const auto assessment = assess_task(update_framework_root, channel, task_id);
    if (assessment.state != DriverDownloadState::ready_to_install &&
        assessment.state != DriverDownloadState::broken_file_location) {
        DriverDownloadRepairResult result;
        result.detail = "The requested download record is not safe to discard";
        return result;
    }
    return remove_download_status_record(update_framework_root, assessment, std::nullopt);
}

} // namespace nvidia_app_cleaner
