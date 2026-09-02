#include "nvidia_app_cleaner/startup_options.h"

#include <algorithm>

namespace nvidia_app_cleaner {
namespace {

std::optional<CacheCategory> parse_cleanup_category(std::wstring_view name) {
    if (name == L"grd") {
        return CacheCategory::game_ready_driver;
    }
    if (name == L"crd") {
        return CacheCategory::studio_driver;
    }
    if (name == L"nvapp") {
        return CacheCategory::nvidia_app_update;
    }
    if (name == L"legacy-update") {
        return CacheCategory::legacy_update_cache;
    }
    if (name == L"legacy-downloader") {
        return CacheCategory::legacy_downloader;
    }
    if (name == L"installer2") {
        return CacheCategory::installer2;
    }
    if (name == L"ngx") {
        return CacheCategory::ngx_models;
    }
    return std::nullopt;
}

const wchar_t *cleanup_category_name(CacheCategory category) {
    switch (category) {
    case CacheCategory::game_ready_driver:
        return L"grd";
    case CacheCategory::studio_driver:
        return L"crd";
    case CacheCategory::nvidia_app_update:
        return L"nvapp";
    case CacheCategory::legacy_update_cache:
        return L"legacy-update";
    case CacheCategory::legacy_downloader:
        return L"legacy-downloader";
    case CacheCategory::installer2:
        return L"installer2";
    case CacheCategory::ngx_models:
        return L"ngx";
    }
    return L"";
}

bool parse_cleanup_list(std::wstring_view list, std::vector<CacheCategory> &categories) {
    if (list.empty()) {
        return false;
    }

    std::size_t position = 0;
    while (position < list.size()) {
        const std::size_t separator = list.find(L',', position);
        const std::wstring_view name =
            list.substr(position, separator == std::wstring_view::npos ? list.size() - position
                                                                       : separator - position);
        const auto category = parse_cleanup_category(name);
        if (!category ||
            std::find(categories.begin(), categories.end(), *category) != categories.end()) {
            return false;
        }
        categories.push_back(*category);
        if (separator == std::wstring_view::npos) {
            return true;
        }
        position = separator + 1;
    }
    return false;
}

bool parse_driver_package_list(std::wstring_view list,
                               std::vector<DriverPackageId> &driver_packages) {
    if (list.empty()) {
        return false;
    }

    std::size_t position = 0;
    while (position < list.size()) {
        const std::size_t separator = list.find(L',', position);
        const std::wstring_view item =
            list.substr(position, separator == std::wstring_view::npos ? list.size() - position
                                                                       : separator - position);
        const std::size_t task_separator = item.find(L':');
        if (task_separator == std::wstring_view::npos ||
            item.find(L':', task_separator + 1) != std::wstring_view::npos) {
            return false;
        }
        const auto category = parse_cleanup_category(item.substr(0, task_separator));
        const std::wstring_view task_id = item.substr(task_separator + 1);
        if (!category || !is_driver_package_category(*category) ||
            !is_valid_driver_package_task_id(task_id)) {
            return false;
        }

        DriverPackageId package{*category, std::wstring(task_id)};
        if (std::find(driver_packages.begin(), driver_packages.end(), package) !=
            driver_packages.end()) {
            return false;
        }
        driver_packages.push_back(std::move(package));
        if (separator == std::wstring_view::npos) {
            return true;
        }
        position = separator + 1;
    }
    return false;
}

} // namespace

StartupOptions parse_startup_options(std::span<const std::wstring_view> arguments) {
    StartupOptions options;
    bool cleanup_argument_seen = false;
    bool driver_packages_argument_seen = false;

    for (const std::wstring_view argument : arguments) {
        if (argument == L"--repair-stuck-downloads") {
            if (options.run_repair_worker) {
                options.valid = false;
            }
            options.run_repair_worker = true;
        } else if (argument.starts_with(L"--cleanup=")) {
            if (cleanup_argument_seen ||
                !parse_cleanup_list(argument.substr(std::wstring_view(L"--cleanup=").size()),
                                    options.cleanup_categories)) {
                options.valid = false;
            }
            cleanup_argument_seen = true;
        } else if (argument.starts_with(L"--driver-packages=")) {
            if (driver_packages_argument_seen ||
                !parse_driver_package_list(
                    argument.substr(std::wstring_view(L"--driver-packages=").size()),
                    options.cleanup_driver_packages)) {
                options.valid = false;
            }
            driver_packages_argument_seen = true;
        } else if (argument == L"--language=en") {
            if (options.language.has_value()) {
                options.valid = false;
            }
            options.language = UiLanguage::english;
        } else if (argument == L"--language=zh-CN") {
            if (options.language.has_value()) {
                options.valid = false;
            }
            options.language = UiLanguage::simplified_chinese;
        } else {
            options.valid = false;
        }
    }

    if (options.run_repair_worker &&
        (!options.cleanup_categories.empty() || !options.cleanup_driver_packages.empty())) {
        options.valid = false;
    }
    for (const auto &package : options.cleanup_driver_packages) {
        if (std::find(options.cleanup_categories.begin(), options.cleanup_categories.end(),
                      package.category) != options.cleanup_categories.end()) {
            options.valid = false;
        }
    }

    return options;
}

std::wstring build_cleanup_argument(std::span<const CacheCategory> cleanup_categories) {
    std::wstring argument = L"--cleanup=";
    bool first = true;
    for (const CacheCategory category : cleanup_categories) {
        if (!first) {
            argument += L',';
        }
        argument += cleanup_category_name(category);
        first = false;
    }
    return argument;
}

std::wstring
build_driver_package_cleanup_argument(std::span<const DriverPackageId> driver_packages) {
    std::wstring argument = L"--driver-packages=";
    bool first = true;
    for (const auto &package : driver_packages) {
        if (!first) {
            argument += L',';
        }
        argument += cleanup_category_name(package.category);
        argument += L':';
        argument += package.task_id;
        first = false;
    }
    return argument;
}

} // namespace nvidia_app_cleaner
