#pragma once

#include "nvidia_app_cleaner/driver_download_repair.h"

#include <windows.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace nvidia_app_cleaner::windows {

[[nodiscard]] std::filesystem::path program_data_path();
[[nodiscard]] std::filesystem::path program_files_path();
[[nodiscard]] std::filesystem::path update_framework_root();
[[nodiscard]] std::filesystem::path create_backup_path(DriverChannel channel);
[[nodiscard]] bool nvidia_app_is_running();

struct InstalledDriverInfo {
    std::wstring gpu_name;
    std::wstring driver_version;
};

[[nodiscard]] std::vector<InstalledDriverInfo> installed_nvidia_drivers();
[[nodiscard]] std::optional<std::wstring> utf8_to_wide(std::string_view text);
[[nodiscard]] std::wstring format_windows_error(std::uint32_t error_code);

enum class ElevatedLaunchResult {
    launched,
    cancelled,
    failed,
};

[[nodiscard]] ElevatedLaunchResult launch_current_executable_elevated(HWND owner,
                                                                      std::wstring_view parameters,
                                                                      bool wait_for_exit,
                                                                      std::uint32_t &error_code);

enum class ServiceErrorKind {
    none,
    windows_error,
    invalid_state,
    timeout,
};

struct ServiceOperationResult {
    ServiceErrorKind error{ServiceErrorKind::none};
    std::uint32_t windows_error{ERROR_SUCCESS};

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == ServiceErrorKind::none;
    }
};

class NvidiaUpdateServicePause {
  public:
    NvidiaUpdateServicePause() = default;
    NvidiaUpdateServicePause(const NvidiaUpdateServicePause &) = delete;
    NvidiaUpdateServicePause &operator=(const NvidiaUpdateServicePause &) = delete;
    ~NvidiaUpdateServicePause();

    [[nodiscard]] ServiceOperationResult pause();
    [[nodiscard]] ServiceOperationResult resume();

  private:
    [[nodiscard]] ServiceOperationResult query(SERVICE_STATUS_PROCESS &status) const;
    [[nodiscard]] ServiceOperationResult wait_for_state(std::uint32_t expected_state) const;

    SC_HANDLE manager_{nullptr};
    SC_HANDLE service_{nullptr};
    bool restart_required_{false};
};

} // namespace nvidia_app_cleaner::windows
