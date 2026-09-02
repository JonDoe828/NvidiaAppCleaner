#pragma once

#include "nvidia_app_cleaner/cache_scanner.h"
#include "nvidia_app_cleaner/localization.h"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace nvidia_app_cleaner {

struct StartupOptions {
    std::optional<UiLanguage> language;
    std::vector<CacheCategory> cleanup_categories;
    std::vector<DriverPackageId> cleanup_driver_packages;
    bool run_repair_worker{false};
    bool valid{true};
};

[[nodiscard]] StartupOptions parse_startup_options(std::span<const std::wstring_view> arguments);
[[nodiscard]] std::wstring
build_cleanup_argument(std::span<const CacheCategory> cleanup_categories);
[[nodiscard]] std::wstring
build_driver_package_cleanup_argument(std::span<const DriverPackageId> driver_packages);

} // namespace nvidia_app_cleaner
