#include "nvidia_app_cleaner/startup_options.h"

#include <iostream>
#include <string_view>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

} // namespace

int main() {
    using nvidia_app_cleaner::CacheCategory;
    using nvidia_app_cleaner::UiLanguage;

    {
        const std::vector<std::wstring_view> arguments;
        const auto options = nvidia_app_cleaner::parse_startup_options(arguments);
        expect(options.valid, "empty command line should be valid");
        expect(!options.run_repair_worker, "normal startup should not select repair worker");
        expect(!options.language.has_value(), "normal startup should use the system language");
        expect(options.cleanup_categories.empty(), "normal startup should not select cleanup");
        expect(options.cleanup_driver_packages.empty(),
               "normal startup should not select driver packages");
    }
    {
        const std::vector<std::wstring_view> arguments{L"--cleanup=grd,nvapp,installer2,ngx",
                                                       L"--language=en"};
        const auto options = nvidia_app_cleaner::parse_startup_options(arguments);
        expect(options.valid, "known cleanup categories should be valid");
        expect(options.cleanup_categories ==
                   std::vector<CacheCategory>{CacheCategory::game_ready_driver,
                                              CacheCategory::nvidia_app_update,
                                              CacheCategory::installer2, CacheCategory::ngx_models},
               "cleanup categories should retain their requested order");
        expect(nvidia_app_cleaner::build_cleanup_argument(options.cleanup_categories) ==
                   L"--cleanup=grd,nvapp,installer2,ngx",
               "cleanup categories should have a stable worker argument");
    }
    {
        const std::vector<std::wstring_view> arguments{
            L"--cleanup=nvapp,installer2",
            L"--driver-packages=grd:772c02f95116cf8e45fbd36539eafbf5,crd:task-2",
            L"--language=zh-CN"};
        const auto options = nvidia_app_cleaner::parse_startup_options(arguments);
        expect(options.valid, "categories and individual driver packages should be composable");
        expect(options.cleanup_driver_packages ==
                   std::vector<nvidia_app_cleaner::DriverPackageId>{
                       {CacheCategory::game_ready_driver, L"772c02f95116cf8e45fbd36539eafbf5"},
                       {CacheCategory::studio_driver, L"task-2"}},
               "driver package taskIds should retain their requested order");
        expect(nvidia_app_cleaner::build_driver_package_cleanup_argument(
                   options.cleanup_driver_packages) ==
                   L"--driver-packages=grd:772c02f95116cf8e45fbd36539eafbf5,crd:task-2",
               "driver package cleanup should have a stable worker argument");
    }
    {
        const std::vector<std::wstring_view> arguments{L"--cleanup=grd,unknown"};
        const auto options = nvidia_app_cleaner::parse_startup_options(arguments);
        expect(!options.valid, "unknown cleanup categories must be rejected");
    }
    {
        const std::vector<std::wstring_view> arguments{L"--cleanup=grd,grd"};
        const auto options = nvidia_app_cleaner::parse_startup_options(arguments);
        expect(!options.valid, "duplicate cleanup categories must be rejected");
    }
    {
        const std::vector<std::wstring_view> arguments{L"--driver-packages=grd:../escape"};
        const auto options = nvidia_app_cleaner::parse_startup_options(arguments);
        expect(!options.valid, "unsafe driver package taskIds must be rejected");
    }
    {
        const std::vector<std::wstring_view> arguments{L"--driver-packages=grd:task-1,grd:task-1"};
        const auto options = nvidia_app_cleaner::parse_startup_options(arguments);
        expect(!options.valid, "duplicate driver packages must be rejected");
    }
    {
        const std::vector<std::wstring_view> arguments{L"--cleanup=grd",
                                                       L"--driver-packages=grd:task-1"};
        const auto options = nvidia_app_cleaner::parse_startup_options(arguments);
        expect(!options.valid,
               "whole-category and per-package cleanup must not overlap for one channel");
    }
    {
        const std::vector<std::wstring_view> arguments{L"--cleanup=grd",
                                                       L"--repair-stuck-downloads"};
        const auto options = nvidia_app_cleaner::parse_startup_options(arguments);
        expect(!options.valid, "cleanup and repair workers must be mutually exclusive");
    }
    {
        const std::vector<std::wstring_view> arguments{L"--repair-stuck-downloads",
                                                       L"--language=zh-CN"};
        const auto options = nvidia_app_cleaner::parse_startup_options(arguments);
        expect(options.valid, "known repair arguments should be valid");
        expect(options.run_repair_worker, "repair argument should select repair worker");
        expect(options.language == UiLanguage::simplified_chinese,
               "Chinese language argument should be parsed exactly");
    }
    {
        const std::vector<std::wstring_view> arguments{L"--repair-stuck-downloads-extra"};
        const auto options = nvidia_app_cleaner::parse_startup_options(arguments);
        expect(!options.valid, "a repair argument suffix must not be accepted");
        expect(!options.run_repair_worker, "an inexact repair argument must not select the worker");
    }
    {
        const std::vector<std::wstring_view> arguments{L"prefix--language=en"};
        const auto options = nvidia_app_cleaner::parse_startup_options(arguments);
        expect(!options.valid, "a language substring must not be accepted");
        expect(!options.language.has_value(), "an inexact language argument must be ignored");
    }
    {
        const std::vector<std::wstring_view> arguments{L"--language=en", L"--language=zh-CN"};
        const auto options = nvidia_app_cleaner::parse_startup_options(arguments);
        expect(!options.valid, "conflicting language arguments should be rejected");
    }

    return failures == 0 ? 0 : 1;
}
