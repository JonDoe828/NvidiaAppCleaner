#include "nvidia_app_cleaner/app_settings.h"

#include <windows.h>

namespace nvidia_app_cleaner {
namespace {

constexpr wchar_t kSettingsKey[] = L"Software\\NvidiaAppCleaner";
constexpr wchar_t kAlwaysRunAsAdministratorValue[] = L"AlwaysRunAsAdministrator";

} // namespace

bool load_app_settings(AppSettings &settings, std::uint32_t &error_code) {
    settings = {};
    error_code = ERROR_SUCCESS;

    HKEY key = nullptr;
    const LSTATUS open_result =
        RegOpenKeyExW(HKEY_CURRENT_USER, kSettingsKey, 0, KEY_QUERY_VALUE, &key);
    if (open_result == ERROR_FILE_NOT_FOUND) {
        return true;
    }
    if (open_result != ERROR_SUCCESS) {
        error_code = static_cast<std::uint32_t>(open_result);
        return false;
    }

    DWORD value = 0;
    DWORD value_size = sizeof(value);
    const LSTATUS query_result = RegGetValueW(key, nullptr, kAlwaysRunAsAdministratorValue,
                                              RRF_RT_REG_DWORD, nullptr, &value, &value_size);
    RegCloseKey(key);

    if (query_result == ERROR_FILE_NOT_FOUND) {
        return true;
    }
    if (query_result != ERROR_SUCCESS) {
        error_code = static_cast<std::uint32_t>(query_result);
        return false;
    }

    settings.always_run_as_administrator = value != 0;
    return true;
}

bool save_app_settings(const AppSettings &settings, std::uint32_t &error_code) {
    error_code = ERROR_SUCCESS;

    HKEY key = nullptr;
    const LSTATUS create_result = RegCreateKeyExW(HKEY_CURRENT_USER, kSettingsKey, 0, nullptr, 0,
                                                  KEY_SET_VALUE, nullptr, &key, nullptr);
    if (create_result != ERROR_SUCCESS) {
        error_code = static_cast<std::uint32_t>(create_result);
        return false;
    }

    const DWORD value = settings.always_run_as_administrator ? 1 : 0;
    const LSTATUS write_result =
        RegSetValueExW(key, kAlwaysRunAsAdministratorValue, 0, REG_DWORD,
                       reinterpret_cast<const BYTE *>(&value), sizeof(value));
    RegCloseKey(key);

    if (write_result != ERROR_SUCCESS) {
        error_code = static_cast<std::uint32_t>(write_result);
        return false;
    }
    return true;
}

bool current_process_is_elevated(bool &elevated, std::uint32_t &error_code) {
    elevated = false;
    error_code = ERROR_SUCCESS;

    HANDLE token = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token) == FALSE) {
        error_code = GetLastError();
        return false;
    }

    TOKEN_ELEVATION elevation{};
    DWORD returned_size = 0;
    const BOOL query_result =
        GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &returned_size);
    const DWORD query_error = query_result == FALSE ? GetLastError() : ERROR_SUCCESS;
    CloseHandle(token);

    if (query_result == FALSE) {
        error_code = query_error;
        return false;
    }

    elevated = elevation.TokenIsElevated != 0;
    return true;
}

} // namespace nvidia_app_cleaner
