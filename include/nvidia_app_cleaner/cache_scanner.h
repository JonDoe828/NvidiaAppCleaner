#pragma once

#include <cstdint>
#include <filesystem>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace nvidia_app_cleaner {

enum class CacheCategory {
    game_ready_driver,
    studio_driver,
    nvidia_app_update,
    legacy_update_cache,
    legacy_downloader,
    installer2,
    ngx_models,
};

[[nodiscard]] constexpr bool is_driver_package_category(CacheCategory category) noexcept {
    return category == CacheCategory::game_ready_driver || category == CacheCategory::studio_driver;
}

[[nodiscard]] constexpr bool is_valid_driver_package_task_id(std::wstring_view value) noexcept {
    if (value.empty() || value.size() > 128) {
        return false;
    }
    for (const wchar_t character : value) {
        const bool valid =
            (character >= L'a' && character <= L'z') || (character >= L'A' && character <= L'Z') ||
            (character >= L'0' && character <= L'9') || character == L'-' || character == L'_';
        if (!valid) {
            return false;
        }
    }
    return true;
}

enum class ScanState {
    ready,
    empty,
    not_found,
    inaccessible,
    unsafe_path,
    partial,
};

struct ScanTarget {
    CacheCategory category;
    std::wstring display_name;
    std::filesystem::path path;
    bool selected_by_default{false};
};

struct DriverPackageId {
    CacheCategory category{CacheCategory::game_ready_driver};
    std::wstring task_id;

    bool operator==(const DriverPackageId &) const = default;
};

struct DriverPackageScanResult {
    DriverPackageId id;
    std::wstring version;
    std::filesystem::path package_path;
    std::filesystem::path post_processing_path;
    std::filesystem::path installer_path;
    std::uintmax_t size_bytes{0};
    std::uintmax_t file_count{0};
};

struct ScanResult {
    ScanTarget target;
    ScanState state{ScanState::not_found};
    std::uintmax_t size_bytes{0};
    std::uintmax_t file_count{0};
    std::uintmax_t skipped_entry_count{0};
    std::vector<std::wstring> package_versions;
    std::vector<DriverPackageScanResult> driver_packages;
    std::string detail;
};

class CacheScanner {
  public:
    [[nodiscard]] ScanResult scan(const ScanTarget &target, std::stop_token stop_token = {}) const;
    [[nodiscard]] std::vector<ScanResult> scan_all(const std::vector<ScanTarget> &targets,
                                                   std::stop_token stop_token = {}) const;
};

[[nodiscard]] std::vector<ScanTarget>
default_scan_targets(const std::filesystem::path &program_data,
                     const std::filesystem::path &program_files);

} // namespace nvidia_app_cleaner
