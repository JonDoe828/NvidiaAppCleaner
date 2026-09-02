#pragma once

#include <cstdint>

namespace nvidia_app_cleaner {

struct AppSettings {
    bool always_run_as_administrator{false};
};

[[nodiscard]] bool load_app_settings(AppSettings &settings, std::uint32_t &error_code);
[[nodiscard]] bool save_app_settings(const AppSettings &settings, std::uint32_t &error_code);
[[nodiscard]] bool current_process_is_elevated(bool &elevated, std::uint32_t &error_code);

} // namespace nvidia_app_cleaner
