#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace nvidia_app_cleaner {

enum class DriverChannel {
    game_ready,
    studio,
    nvidia_app,
};

enum class DriverDownloadState {
    no_record,
    downloading,
    ready_to_install,
    broken_file_location,
    unsupported_record,
    inaccessible,
};

struct DriverDownloadAssessment {
    DriverChannel channel{DriverChannel::game_ready};
    DriverDownloadState state{DriverDownloadState::no_record};
    std::filesystem::path package_path;
    std::string version;
    std::string task_id;
    std::string detail;
};

struct DriverDownloadRepairResult {
    bool repaired{false};
    std::filesystem::path backup_directory;
    std::string detail;
};

class DriverDownloadRepair {
  public:
    [[nodiscard]] DriverDownloadAssessment
    assess(const std::filesystem::path &update_framework_root, DriverChannel channel) const;

    [[nodiscard]] DriverDownloadAssessment
    assess_task(const std::filesystem::path &update_framework_root, DriverChannel channel,
                std::string_view task_id) const;

    [[nodiscard]] DriverDownloadRepairResult
    repair(const std::filesystem::path &update_framework_root, DriverChannel channel,
           const std::filesystem::path &backup_directory) const;

    [[nodiscard]] DriverDownloadRepairResult
    discard_completed_download(const std::filesystem::path &update_framework_root,
                               DriverChannel channel) const;

    [[nodiscard]] DriverDownloadRepairResult
    discard_completed_download_task(const std::filesystem::path &update_framework_root,
                                    DriverChannel channel, std::string_view task_id) const;
};

[[nodiscard]] const wchar_t *driver_channel_directory(DriverChannel channel);

} // namespace nvidia_app_cleaner
