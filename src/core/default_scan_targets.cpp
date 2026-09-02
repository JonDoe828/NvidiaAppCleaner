#include "nvidia_app_cleaner/cache_scanner.h"

namespace nvidia_app_cleaner {

std::vector<ScanTarget> default_scan_targets(const std::filesystem::path &program_data,
                                             const std::filesystem::path &program_files) {
    const auto nvidia_root = program_data / L"NVIDIA Corporation";
    const auto ota_root = nvidia_root / L"NVIDIA App" / L"UpdateFramework" / L"ota-artifacts";

    return {
        {CacheCategory::game_ready_driver, L"Game Ready / rollback driver packages",
         ota_root / L"grd", false},
        {CacheCategory::studio_driver, L"Studio / rollback driver packages", ota_root / L"crd",
         false},
        {CacheCategory::nvidia_app_update, L"NVIDIA App update packages", ota_root / L"nvapp",
         true},
        {CacheCategory::legacy_update_cache, L"Legacy NVIDIA App update cache",
         nvidia_root / L"NvApp-UpdateFramework" / L"ota-artifacts", true},
        {CacheCategory::legacy_downloader, L"Legacy driver downloader cache",
         nvidia_root / L"Downloader", true},
        {CacheCategory::installer2, L"NVIDIA Installer2 maintenance cache",
         program_files / L"NVIDIA Corporation" / L"Installer2", false},
        {CacheCategory::ngx_models, L"NVIDIA NGX downloaded models",
         program_data / L"NVIDIA" / L"NGX" / L"models", false},
    };
}

} // namespace nvidia_app_cleaner
