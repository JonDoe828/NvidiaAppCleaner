#include "nvidia_app_cleaner/cache_scanner.h"

#include <algorithm>
#include <cwctype>
#include <optional>
#include <string_view>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#endif

namespace nvidia_app_cleaner {
namespace {

bool is_reparse_point(const std::filesystem::path &path, std::error_code &error) {
#ifdef _WIN32
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        error = std::error_code(static_cast<int>(GetLastError()), std::system_category());
        return false;
    }
    error.clear();
    return (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
    const auto status = std::filesystem::symlink_status(path, error);
    return !error && std::filesystem::is_symlink(status);
#endif
}

void record_error(ScanResult &result, const std::error_code &error) {
    result.state = ScanState::partial;
    if (result.detail.empty()) {
        result.detail = error.message();
    }
}

bool can_contain_driver_installer(CacheCategory category) {
    return category == CacheCategory::game_ready_driver ||
           category == CacheCategory::studio_driver || category == CacheCategory::legacy_downloader;
}

bool is_ascii_digit(wchar_t character) { return character >= L'0' && character <= L'9'; }

std::wstring driver_package_version(const std::filesystem::path &path) {
    std::wstring extension = path.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](wchar_t character) { return std::towlower(character); });
    if (extension != L".exe") {
        return {};
    }

    const std::wstring filename = path.stem().wstring();
    std::size_t major_digits = 0;
    while (major_digits < filename.size() && is_ascii_digit(filename[major_digits])) {
        ++major_digits;
    }
    if (major_digits < 3 || major_digits > 4 || major_digits >= filename.size() ||
        filename[major_digits] != L'.') {
        return {};
    }

    const std::size_t minor_start = major_digits + 1;
    std::size_t minor_end = minor_start;
    while (minor_end < filename.size() && is_ascii_digit(filename[minor_end])) {
        ++minor_end;
    }
    if (minor_end - minor_start != 2 ||
        (minor_end < filename.size() && filename[minor_end] != L'-' &&
         filename[minor_end] != L'_')) {
        return {};
    }
    return filename.substr(0, minor_end);
}

unsigned int driver_version_sort_key(std::wstring_view version) {
    const std::size_t separator = version.find(L'.');
    if (separator == std::wstring_view::npos) {
        return 0;
    }

    unsigned int major = 0;
    for (const wchar_t character : version.substr(0, separator)) {
        if (!is_ascii_digit(character)) {
            return 0;
        }
        major = major * 10U + static_cast<unsigned int>(character - L'0');
    }

    unsigned int minor = 0;
    for (const wchar_t character : version.substr(separator + 1)) {
        if (!is_ascii_digit(character)) {
            return 0;
        }
        minor = minor * 10U + static_cast<unsigned int>(character - L'0');
    }
    return major * 100U + minor;
}

void record_package_version(ScanResult &result, const std::filesystem::path &path) {
    if (!can_contain_driver_installer(result.target.category)) {
        return;
    }
    const std::wstring version = driver_package_version(path);
    if (version.empty() || std::find(result.package_versions.begin(), result.package_versions.end(),
                                     version) != result.package_versions.end()) {
        return;
    }
    result.package_versions.push_back(version);
}

std::optional<std::wstring> package_task_id(const ScanResult &result,
                                            const std::filesystem::path &path) {
    if (!is_driver_package_category(result.target.category)) {
        return std::nullopt;
    }

    const auto relative = path.lexically_relative(result.target.path);
    auto component = relative.begin();
    if (component == relative.end()) {
        return std::nullopt;
    }
    if (*component == L"post-processing") {
        ++component;
        if (component == relative.end()) {
            return std::nullopt;
        }
    }

    const std::wstring task_id = component->wstring();
    ++component;
    if (component == relative.end()) {
        return std::nullopt;
    }
    return is_valid_driver_package_task_id(task_id) ? std::optional<std::wstring>(task_id)
                                                    : std::nullopt;
}

void record_driver_package_file(ScanResult &result, const std::filesystem::path &path,
                                std::uintmax_t file_size) {
    const auto task_id = package_task_id(result, path);
    if (!task_id) {
        return;
    }

    auto package = std::find_if(
        result.driver_packages.begin(), result.driver_packages.end(),
        [&](const DriverPackageScanResult &candidate) { return candidate.id.task_id == *task_id; });
    if (package == result.driver_packages.end()) {
        DriverPackageScanResult discovered;
        discovered.id = {result.target.category, *task_id};
        discovered.package_path = result.target.path / *task_id;
        discovered.post_processing_path = result.target.path / L"post-processing" / *task_id;
        result.driver_packages.push_back(std::move(discovered));
        package = std::prev(result.driver_packages.end());
    }

    package->size_bytes += file_size;
    ++package->file_count;
    const std::wstring version = driver_package_version(path);
    if (!version.empty() && package->version.empty()) {
        package->version = version;
        package->installer_path = path;
    }
}

} // namespace

ScanResult CacheScanner::scan(const ScanTarget &target, std::stop_token stop_token) const {
    ScanResult result{};
    result.target = target;

    if (stop_token.stop_requested()) {
        return result;
    }

    std::error_code error;
    const auto root_status = std::filesystem::symlink_status(target.path, error);
    if (error) {
        if (error == std::errc::no_such_file_or_directory) {
            result.state = ScanState::not_found;
        } else {
            result.state = ScanState::inaccessible;
            result.detail = error.message();
        }
        return result;
    }

    if (!std::filesystem::exists(root_status)) {
        result.state = ScanState::not_found;
        return result;
    }

    if (is_reparse_point(target.path, error)) {
        result.state = ScanState::unsafe_path;
        result.detail = "The scan root is a symbolic link or directory junction";
        return result;
    }
    if (error) {
        result.state = ScanState::inaccessible;
        result.detail = error.message();
        return result;
    }
    if (!std::filesystem::is_directory(root_status)) {
        result.state = ScanState::unsafe_path;
        result.detail = "The scan root is not a normal directory";
        return result;
    }

    result.state = ScanState::ready;
    std::filesystem::recursive_directory_iterator iterator(
        target.path, std::filesystem::directory_options::skip_permission_denied, error);
    const std::filesystem::recursive_directory_iterator end;

    if (error) {
        result.state = ScanState::inaccessible;
        result.detail = error.message();
        return result;
    }

    while (iterator != end) {
        if (stop_token.stop_requested()) {
            return result;
        }

        if (is_reparse_point(iterator->path(), error)) {
            iterator.disable_recursion_pending();
            ++result.skipped_entry_count;
            result.state = ScanState::partial;
            if (result.detail.empty()) {
                result.detail = "Symbolic links or directory junctions were skipped";
            }
            iterator.increment(error);
            if (error) {
                record_error(result, error);
                ++result.skipped_entry_count;
                break;
            }
            continue;
        }
        if (error) {
            record_error(result, error);
            ++result.skipped_entry_count;
            error.clear();
        }

        const auto entry_status = iterator->symlink_status(error);
        if (error) {
            record_error(result, error);
            ++result.skipped_entry_count;
            error.clear();
        } else if (std::filesystem::is_regular_file(entry_status)) {
            const auto file_size = iterator->file_size(error);
            if (error) {
                record_error(result, error);
                ++result.skipped_entry_count;
                error.clear();
            } else {
                result.size_bytes += file_size;
                ++result.file_count;
                record_package_version(result, iterator->path());
                record_driver_package_file(result, iterator->path(), file_size);
            }
        }

        iterator.increment(error);
        if (error) {
            record_error(result, error);
            ++result.skipped_entry_count;
            break;
        }
    }

    if (result.state == ScanState::ready && result.file_count == 0) {
        result.state = ScanState::empty;
    }
    std::sort(result.package_versions.begin(), result.package_versions.end(),
              [](std::wstring_view left, std::wstring_view right) {
                  return driver_version_sort_key(left) > driver_version_sort_key(right);
              });
    std::sort(result.driver_packages.begin(), result.driver_packages.end(),
              [](const DriverPackageScanResult &left, const DriverPackageScanResult &right) {
                  if (left.version != right.version) {
                      return driver_version_sort_key(left.version) >
                             driver_version_sort_key(right.version);
                  }
                  return left.id.task_id < right.id.task_id;
              });

    return result;
}

std::vector<ScanResult> CacheScanner::scan_all(const std::vector<ScanTarget> &targets,
                                               std::stop_token stop_token) const {
    std::vector<ScanResult> results;
    results.reserve(targets.size());

    for (const auto &target : targets) {
        if (stop_token.stop_requested()) {
            break;
        }
        results.push_back(scan(target, stop_token));
    }

    return results;
}

} // namespace nvidia_app_cleaner
