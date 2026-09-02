#pragma once

#include "nvidia_app_cleaner/cache_scanner.h"
#include "nvidia_app_cleaner/localization.h"

#include <windows.h>

#include <span>

namespace nvidia_app_cleaner::ui {

[[nodiscard]] int run_main_window(HINSTANCE instance, int show_command, UiLanguage language);
[[nodiscard]] int run_elevated_repair_worker(UiLanguage language);
[[nodiscard]] int run_elevated_cleanup_worker(UiLanguage language,
                                              std::span<const CacheCategory> categories,
                                              std::span<const DriverPackageId> driver_packages);

} // namespace nvidia_app_cleaner::ui
