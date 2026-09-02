#include "nvidia_app_cleaner/cache_cleaner.h"

#include <algorithm>
#include <array>
#include <system_error>
#include <utility>

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

void record_failure(CleanupResult &result, const std::error_code &error) {
    ++result.failed_entry_count;
    if (result.detail.empty()) {
        result.detail = error.message();
    }
}

bool remove_directory_contents(const std::filesystem::path &directory, CleanupResult &result) {
    std::error_code error;
    std::filesystem::directory_iterator iterator(directory, error);
    const std::filesystem::directory_iterator end;
    if (error) {
        record_failure(result, error);
        return false;
    }

    bool complete = true;
    while (iterator != end) {
        const auto entry = *iterator;
        iterator.increment(error);
        if (error) {
            record_failure(result, error);
            error.clear();
            complete = false;
        }

        if (is_reparse_point(entry.path(), error)) {
            ++result.failed_entry_count;
            if (result.detail.empty()) {
                result.detail = "A symbolic link or directory junction appeared during cleanup";
            }
            complete = false;
            error.clear();
            continue;
        }
        if (error) {
            record_failure(result, error);
            error.clear();
            complete = false;
            continue;
        }

        const auto status = entry.symlink_status(error);
        if (error) {
            record_failure(result, error);
            error.clear();
            complete = false;
            continue;
        }

        if (std::filesystem::is_directory(status)) {
            if (!remove_directory_contents(entry.path(), result)) {
                complete = false;
                continue;
            }
            if (!std::filesystem::remove(entry.path(), error)) {
                if (!error) {
                    error = std::make_error_code(std::errc::directory_not_empty);
                }
                record_failure(result, error);
                error.clear();
                complete = false;
            }
            continue;
        }

        if (!std::filesystem::is_regular_file(status)) {
            ++result.failed_entry_count;
            if (result.detail.empty()) {
                result.detail = "An unsupported filesystem entry was not removed";
            }
            complete = false;
            continue;
        }

        const auto file_size = entry.file_size(error);
        if (error) {
            record_failure(result, error);
            error.clear();
            complete = false;
            continue;
        }
        if (!std::filesystem::remove(entry.path(), error)) {
            if (!error) {
                error = std::make_error_code(std::errc::operation_not_permitted);
            }
            record_failure(result, error);
            error.clear();
            complete = false;
            continue;
        }
        result.bytes_removed += file_size;
        ++result.files_removed;
    }
    return complete;
}

} // namespace

CacheCleaner::CacheCleaner(std::vector<ScanTarget> approved_targets)
    : approved_targets_(std::move(approved_targets)) {}

CleanupResult CacheCleaner::clean(CacheCategory category) const {
    CleanupResult result;
    result.category = category;
    const auto target = std::find_if(
        approved_targets_.begin(), approved_targets_.end(),
        [category](const ScanTarget &candidate) { return candidate.category == category; });
    if (target == approved_targets_.end() || target->path.empty()) {
        result.state = CleanupState::not_approved;
        result.detail = "The requested cleanup category is not in the approved target list";
        return result;
    }

    std::error_code error;
    if (is_reparse_point(target->path, error)) {
        result.state = CleanupState::unsafe_path;
        result.detail = "The cleanup root is a symbolic link or directory junction";
        return result;
    }
    if (error) {
        result.state = error == std::errc::no_such_file_or_directory ? CleanupState::not_found
                                                                     : CleanupState::failed;
        result.detail = error.message();
        return result;
    }

    const auto status = std::filesystem::symlink_status(target->path, error);
    if (error) {
        result.state = error == std::errc::no_such_file_or_directory ? CleanupState::not_found
                                                                     : CleanupState::failed;
        result.detail = error.message();
        return result;
    }
    if (!std::filesystem::exists(status)) {
        result.state = CleanupState::not_found;
        return result;
    }
    if (!std::filesystem::is_directory(status)) {
        result.state = CleanupState::unsafe_path;
        result.detail = "The cleanup root is not a normal directory";
        return result;
    }

    const CacheScanner scanner;
    const auto preflight = scanner.scan(*target);
    if (preflight.state == ScanState::unsafe_path || preflight.state == ScanState::partial) {
        result.state = CleanupState::unsafe_path;
        result.detail = preflight.detail;
        return result;
    }
    if (preflight.state == ScanState::inaccessible) {
        result.state = CleanupState::failed;
        result.detail = preflight.detail;
        return result;
    }
    if (preflight.state == ScanState::empty) {
        result.state = CleanupState::nothing_to_do;
        return result;
    }

    const bool complete = remove_directory_contents(target->path, result);
    if (complete) {
        result.state = CleanupState::cleaned;
    } else if (result.files_removed > 0) {
        result.state = CleanupState::partial;
    } else {
        result.state = CleanupState::failed;
    }
    return result;
}

CleanupResult CacheCleaner::clean_driver_package(const DriverPackageId &package) const {
    CleanupResult result;
    result.category = package.category;
    if (!is_driver_package_category(package.category) ||
        !is_valid_driver_package_task_id(package.task_id)) {
        result.state = CleanupState::not_approved;
        result.detail = "The requested driver package identifier is not valid";
        return result;
    }

    const auto target =
        std::find_if(approved_targets_.begin(), approved_targets_.end(),
                     [&](const auto &candidate) { return candidate.category == package.category; });
    if (target == approved_targets_.end() || target->path.empty()) {
        result.state = CleanupState::not_approved;
        result.detail = "The requested driver package category is not approved";
        return result;
    }

    const CacheScanner scanner;
    const auto preflight = scanner.scan(*target);
    if (preflight.state == ScanState::not_found) {
        result.state = CleanupState::not_found;
        return result;
    }
    if (preflight.state == ScanState::empty) {
        result.state = CleanupState::nothing_to_do;
        return result;
    }
    if (preflight.state == ScanState::unsafe_path || preflight.state == ScanState::partial) {
        result.state = CleanupState::unsafe_path;
        result.detail = preflight.detail;
        return result;
    }
    if (preflight.state == ScanState::inaccessible) {
        result.state = CleanupState::failed;
        result.detail = preflight.detail;
        return result;
    }

    const std::array package_paths{
        target->path / package.task_id,
        target->path / L"post-processing" / package.task_id,
    };
    std::vector<std::filesystem::path> existing_paths;
    for (const auto &path : package_paths) {
        std::error_code error;
        const auto status = std::filesystem::symlink_status(path, error);
        if (error) {
            if (error == std::errc::no_such_file_or_directory) {
                continue;
            }
            result.state = CleanupState::failed;
            result.detail = error.message();
            return result;
        }
        if (!std::filesystem::exists(status)) {
            continue;
        }
        if (std::filesystem::is_symlink(status) || !std::filesystem::is_directory(status)) {
            result.state = CleanupState::unsafe_path;
            result.detail = "A driver package path is not a normal directory";
            return result;
        }
        existing_paths.push_back(path);
    }
    if (existing_paths.empty()) {
        result.state = CleanupState::not_found;
        return result;
    }

    bool complete = true;
    for (const auto &path : existing_paths) {
        if (!remove_directory_contents(path, result)) {
            complete = false;
            continue;
        }
        std::error_code error;
        if (!std::filesystem::remove(path, error)) {
            if (!error) {
                error = std::make_error_code(std::errc::directory_not_empty);
            }
            record_failure(result, error);
            complete = false;
        }
    }

    if (complete) {
        result.state = CleanupState::cleaned;
    } else if (result.files_removed > 0) {
        result.state = CleanupState::partial;
    } else {
        result.state = CleanupState::failed;
    }
    return result;
}

} // namespace nvidia_app_cleaner
