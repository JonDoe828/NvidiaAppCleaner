#pragma once

#include "nvidia_app_cleaner/cache_scanner.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace nvidia_app_cleaner {

enum class CleanupState {
    cleaned,
    nothing_to_do,
    not_found,
    not_approved,
    unsafe_path,
    failed,
    partial,
};

struct CleanupResult {
    CacheCategory category{CacheCategory::game_ready_driver};
    CleanupState state{CleanupState::failed};
    std::uintmax_t bytes_removed{0};
    std::uintmax_t files_removed{0};
    std::uintmax_t failed_entry_count{0};
    std::string detail;
};

class CacheCleaner {
  public:
    explicit CacheCleaner(std::vector<ScanTarget> approved_targets);

    [[nodiscard]] CleanupResult clean(CacheCategory category) const;
    [[nodiscard]] CleanupResult clean_driver_package(const DriverPackageId &package) const;

  private:
    std::vector<ScanTarget> approved_targets_;
};

} // namespace nvidia_app_cleaner
