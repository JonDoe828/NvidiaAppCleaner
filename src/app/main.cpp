#include "nvidia_app_cleaner/app_settings.h"
#include "nvidia_app_cleaner/localization.h"
#include "nvidia_app_cleaner/startup_options.h"
#include "platform/windows/windows_platform.h"
#include "ui/main_window.h"

#include <windows.h>

#include <shellapi.h>
#include <winrt/Windows.Foundation.h>

#include <string>
#include <string_view>
#include <vector>

namespace {

nvidia_app_cleaner::StartupOptions process_startup_options() {
    int argument_count = 0;
    wchar_t **raw_arguments = CommandLineToArgvW(GetCommandLineW(), &argument_count);
    if (raw_arguments == nullptr || argument_count < 1) {
        if (raw_arguments != nullptr) {
            LocalFree(raw_arguments);
        }
        return {.valid = false};
    }

    std::vector<std::wstring_view> arguments;
    arguments.reserve(static_cast<std::size_t>(argument_count - 1));
    for (int index = 1; index < argument_count; ++index) {
        arguments.emplace_back(raw_arguments[index]);
    }
    const auto options = nvidia_app_cleaner::parse_startup_options(arguments);
    LocalFree(raw_arguments);
    return options;
}

std::wstring language_argument(nvidia_app_cleaner::UiLanguage language) {
    return language == nvidia_app_cleaner::UiLanguage::simplified_chinese ? L"--language=zh-CN"
                                                                          : L"--language=en";
}

std::wstring windows_error_message(nvidia_app_cleaner::UiLanguage language,
                                   std::uint32_t error_code) {
    const std::wstring message = nvidia_app_cleaner::windows::format_windows_error(error_code);
    if (!message.empty()) {
        return message;
    }
    return std::wstring(nvidia_app_cleaner::text(
               language, nvidia_app_cleaner::TextId::windows_error_prefix)) +
           std::to_wstring(error_code);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_command) {
    const auto startup_options = process_startup_options();
    if (!startup_options.valid) {
        return ERROR_BAD_ARGUMENTS;
    }

    const nvidia_app_cleaner::UiLanguage language =
        startup_options.language.value_or(nvidia_app_cleaner::system_ui_language());
    if (startup_options.run_repair_worker || !startup_options.cleanup_categories.empty() ||
        !startup_options.cleanup_driver_packages.empty()) {
        bool elevated = false;
        std::uint32_t elevation_error = ERROR_SUCCESS;
        if (!nvidia_app_cleaner::current_process_is_elevated(elevated, elevation_error)) {
            return static_cast<int>(elevation_error);
        }
        if (!elevated) {
            return ERROR_ELEVATION_REQUIRED;
        }
        if (!startup_options.cleanup_categories.empty() ||
            !startup_options.cleanup_driver_packages.empty()) {
            return nvidia_app_cleaner::ui::run_elevated_cleanup_worker(
                language, startup_options.cleanup_categories,
                startup_options.cleanup_driver_packages);
        }
        return nvidia_app_cleaner::ui::run_elevated_repair_worker(language);
    }

    bool apartment_initialized = false;
    try {
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        apartment_initialized = true;
    } catch (...) {
    }

    const auto tr = [language](nvidia_app_cleaner::TextId id) {
        return nvidia_app_cleaner::text(language, id);
    };
    nvidia_app_cleaner::AppSettings settings;
    std::uint32_t settings_error = ERROR_SUCCESS;
    if (!nvidia_app_cleaner::load_app_settings(settings, settings_error)) {
        const std::wstring message = tr(nvidia_app_cleaner::TextId::settings_read_failed) +
                                     windows_error_message(language, settings_error);
        MessageBoxW(nullptr, message.c_str(), tr(nvidia_app_cleaner::TextId::window_title),
                    MB_OK | MB_ICONERROR);
    } else if (settings.always_run_as_administrator) {
        bool elevated = false;
        if (!nvidia_app_cleaner::current_process_is_elevated(elevated, settings_error)) {
            const std::wstring message =
                tr(nvidia_app_cleaner::TextId::automatic_elevation_failed) +
                windows_error_message(language, settings_error);
            MessageBoxW(nullptr, message.c_str(), tr(nvidia_app_cleaner::TextId::window_title),
                        MB_OK | MB_ICONERROR);
        } else if (!elevated) {
            std::uint32_t launch_error = ERROR_SUCCESS;
            const auto launch_result =
                nvidia_app_cleaner::windows::launch_current_executable_elevated(
                    nullptr, language_argument(language), false, launch_error);
            if (launch_result != nvidia_app_cleaner::windows::ElevatedLaunchResult::failed) {
                if (apartment_initialized) {
                    winrt::uninit_apartment();
                }
                return 0;
            }

            const std::wstring message =
                tr(nvidia_app_cleaner::TextId::automatic_elevation_failed) +
                windows_error_message(language, launch_error);
            MessageBoxW(nullptr, message.c_str(), tr(nvidia_app_cleaner::TextId::window_title),
                        MB_OK | MB_ICONERROR);
        }
    }

    const int result = nvidia_app_cleaner::ui::run_main_window(instance, show_command, language);
    if (apartment_initialized) {
        winrt::uninit_apartment();
    }
    return result;
}
