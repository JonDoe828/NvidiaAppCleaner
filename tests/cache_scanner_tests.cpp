#include "nvidia_app_cleaner/cache_cleaner.h"
#include "nvidia_app_cleaner/cache_scanner.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <stop_token>
#include <string>

namespace {

class TemporaryDirectory {
  public:
    TemporaryDirectory() {
        const auto unique_value = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
                ("nvidia_app_cleaner_tests_" + std::to_string(unique_value));
        std::filesystem::create_directories(path_);
    }

    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    TemporaryDirectory(const TemporaryDirectory &) = delete;
    TemporaryDirectory &operator=(const TemporaryDirectory &) = delete;

    [[nodiscard]] const std::filesystem::path &path() const { return path_; }

  private:
    std::filesystem::path path_;
};

void expect(bool condition, const std::string &message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void write_file(const std::filesystem::path &path, std::size_t size) {
    std::ofstream output(path, std::ios::binary);
    output << std::string(size, 'x');
}

nvidia_app_cleaner::ScanTarget target_for(const std::filesystem::path &path) {
    return {
        nvidia_app_cleaner::CacheCategory::game_ready_driver,
        L"Test cache",
        path,
    };
}

void scans_nested_files() {
    TemporaryDirectory temporary_directory;
    const auto nested = temporary_directory.path() / "nested";
    std::filesystem::create_directory(nested);
    write_file(temporary_directory.path() / "one.bin", 4);
    write_file(nested / "two.bin", 7);

    const nvidia_app_cleaner::CacheScanner scanner;
    const auto result = scanner.scan(target_for(temporary_directory.path()));

    expect(result.state == nvidia_app_cleaner::ScanState::ready,
           "populated directory should be ready");
    expect(result.file_count == 2, "scanner should count nested files");
    expect(result.size_bytes == 11, "scanner should total file sizes");
}

void lists_distinct_driver_package_versions() {
    TemporaryDirectory temporary_directory;
    const auto nested = temporary_directory.path() / "nested";
    std::filesystem::create_directory(nested);
    write_file(temporary_directory.path() /
                   "610.88-desktop-win10-win11-64bit-international-dch-whql-g.exe",
               4);
    write_file(nested / "609.42-notebook-win10-win11-64bit.exe", 5);
    write_file(nested / "610.88-second-copy.exe", 6);
    write_file(nested / "setup.exe", 7);
    write_file(nested / "610.88-not-an-installer.zip", 8);
    write_file(nested / "1000.00-future-driver.exe", 9);

    const nvidia_app_cleaner::CacheScanner scanner;
    const auto result = scanner.scan(target_for(temporary_directory.path()));

    expect(result.package_versions == std::vector<std::wstring>{L"1000.00", L"610.88", L"609.42"},
           "scanner should list distinct driver installer versions in numeric descending order");
}

void groups_driver_files_by_task_id() {
    TemporaryDirectory temporary_directory;
    const auto first_package = temporary_directory.path() / "task-a";
    const auto first_extracted = temporary_directory.path() / "post-processing" / "task-a";
    const auto second_package = temporary_directory.path() / "task-b";
    std::filesystem::create_directories(first_package);
    std::filesystem::create_directories(first_extracted);
    std::filesystem::create_directories(second_package);
    write_file(first_package / "610.88-driver.exe", 4);
    write_file(first_extracted / "setup.exe", 7);
    write_file(second_package / "609.42-driver.exe", 5);

    const nvidia_app_cleaner::CacheScanner scanner;
    const auto result = scanner.scan(target_for(temporary_directory.path()));

    expect(result.driver_packages.size() == 2,
           "driver files should be grouped into individual taskId packages");
    expect(result.driver_packages[0].id.task_id == L"task-a" &&
               result.driver_packages[0].version == L"610.88" &&
               result.driver_packages[0].size_bytes == 11 &&
               result.driver_packages[0].file_count == 2,
           "raw and post-processing files for one taskId should form one package");
    expect(result.driver_packages[1].id.task_id == L"task-b" &&
               result.driver_packages[1].version == L"609.42",
           "each taskId should remain separately selectable");
}

void ignores_files_not_nested_under_task_id() {
    TemporaryDirectory temporary_directory;
    const auto post_processing = temporary_directory.path() / "post-processing";
    std::filesystem::create_directory(post_processing);
    write_file(temporary_directory.path() / "orphan", 4);
    write_file(post_processing / "orphan", 5);

    const nvidia_app_cleaner::CacheScanner scanner;
    const auto result = scanner.scan(target_for(temporary_directory.path()));

    expect(result.file_count == 2, "ordinary files should still contribute to the cache size");
    expect(result.driver_packages.empty(),
           "files outside a taskId directory must not become selectable driver packages");
}

void validates_driver_package_identifiers() {
    using nvidia_app_cleaner::CacheCategory;

    expect(nvidia_app_cleaner::is_driver_package_category(CacheCategory::game_ready_driver) &&
               nvidia_app_cleaner::is_driver_package_category(CacheCategory::studio_driver),
           "both NVIDIA driver channels should support selectable packages");
    expect(!nvidia_app_cleaner::is_driver_package_category(CacheCategory::nvidia_app_update),
           "non-driver categories must not accept driver package identifiers");

    std::wstring maximum_length(128, L'a');
    expect(nvidia_app_cleaner::is_valid_driver_package_task_id(L"task_610-88") &&
               nvidia_app_cleaner::is_valid_driver_package_task_id(maximum_length),
           "safe NVIDIA taskId values should be accepted");
    maximum_length.push_back(L'a');
    expect(!nvidia_app_cleaner::is_valid_driver_package_task_id(L"") &&
               !nvidia_app_cleaner::is_valid_driver_package_task_id(L"../task") &&
               !nvidia_app_cleaner::is_valid_driver_package_task_id(L"任务") &&
               !nvidia_app_cleaner::is_valid_driver_package_task_id(maximum_length),
           "unsafe or unsupported taskId values should be rejected");
}

void ignores_versions_outside_driver_package_categories() {
    TemporaryDirectory temporary_directory;
    write_file(temporary_directory.path() / "610.88-update.exe", 4);
    auto target = target_for(temporary_directory.path());
    target.category = nvidia_app_cleaner::CacheCategory::nvidia_app_update;

    const nvidia_app_cleaner::CacheScanner scanner;
    const auto result = scanner.scan(target);

    expect(result.package_versions.empty(),
           "non-driver caches must not report files as driver packages");
}

void reports_empty_directory() {
    TemporaryDirectory temporary_directory;
    const nvidia_app_cleaner::CacheScanner scanner;
    const auto result = scanner.scan(target_for(temporary_directory.path()));

    expect(result.state == nvidia_app_cleaner::ScanState::empty,
           "empty directory should be reported as empty");
}

void reports_missing_directory() {
    TemporaryDirectory temporary_directory;
    const nvidia_app_cleaner::CacheScanner scanner;
    const auto result = scanner.scan(target_for(temporary_directory.path() / "does-not-exist"));

    expect(result.state == nvidia_app_cleaner::ScanState::not_found,
           "missing directory should be reported as not found");
}

void rejects_non_directory_root() {
    TemporaryDirectory temporary_directory;
    const auto file = temporary_directory.path() / "cache.bin";
    write_file(file, 8);

    const nvidia_app_cleaner::CacheScanner scanner;
    const auto result = scanner.scan(target_for(file));

    expect(result.state == nvidia_app_cleaner::ScanState::unsafe_path,
           "a scan root must be a normal directory");
    expect(result.size_bytes == 0, "an unsafe scan root must not contribute bytes");
}

void skips_symbolic_links() {
    TemporaryDirectory temporary_directory;
    const auto cache = temporary_directory.path() / "cache";
    const auto external = temporary_directory.path() / "external";
    std::filesystem::create_directories(cache);
    std::filesystem::create_directories(external);
    write_file(external / "outside.bin", 32);

    std::error_code error;
    std::filesystem::create_directory_symlink(external, cache / "linked", error);
    if (error) {
        std::cout << "Symbolic-link test skipped: " << error.message() << '\n';
        return;
    }

    const nvidia_app_cleaner::CacheScanner scanner;
    const auto result = scanner.scan(target_for(cache));

    expect(result.state == nvidia_app_cleaner::ScanState::partial,
           "directory with a symbolic link should be partial");
    expect(result.skipped_entry_count == 1, "symbolic link should be counted as skipped");
    expect(result.file_count == 0, "scanner must not count files reached through a symbolic link");
    expect(result.size_bytes == 0,
           "scanner must not include bytes reached through a symbolic link");
}

void builds_expected_default_targets() {
    const std::filesystem::path program_data = L"C:\\ProgramData";
    const std::filesystem::path program_files = L"C:\\Program Files";
    const auto targets = nvidia_app_cleaner::default_scan_targets(program_data, program_files);

    expect(targets.size() == 7, "seven cleanup targets are expected");
    expect(targets.front().path.filename() == L"grd",
           "first target should be the Game Ready Driver cache");
    expect(!targets.front().selected_by_default,
           "driver rollback packages must not be selected by default");
    expect(targets[2].selected_by_default,
           "ordinary NVIDIA App update packages should be selected by default");
    expect(targets[5].path == program_files / L"NVIDIA Corporation" / L"Installer2",
           "Installer2 should use the native Program Files directory");
    expect(!targets[5].selected_by_default, "Installer2 must be optional");
    expect(targets.back().path == program_data / L"NVIDIA" / L"NGX" / L"models",
           "the NGX target should contain only downloaded models");
    expect(!targets.back().selected_by_default, "NGX models must be optional");
}

void honours_pre_cancelled_scan() {
    TemporaryDirectory temporary_directory;
    std::stop_source stop_source;
    stop_source.request_stop();

    const nvidia_app_cleaner::CacheScanner scanner;
    const auto results =
        scanner.scan_all({target_for(temporary_directory.path())}, stop_source.get_token());

    expect(results.empty(), "a scan cancelled before starting should return no results");
}

void simulates_recommended_cleanup_workflow() {
    TemporaryDirectory temporary_directory;
    const auto program_data = temporary_directory.path() / "ProgramData";
    const auto program_files = temporary_directory.path() / "ProgramFiles";
    const auto outside_file = temporary_directory.path() / "outside-user-file.bin";
    write_file(outside_file, 64);

    const auto targets = nvidia_app_cleaner::default_scan_targets(program_data, program_files);
    std::uintmax_t expected_total_bytes = 0;
    for (std::size_t index = 0; index < targets.size(); ++index) {
        const std::size_t file_size = index + 1;
        std::filesystem::create_directories(targets[index].path / "nested");
        write_file(targets[index].path / "nested" / "payload.bin", file_size);
        expected_total_bytes += file_size;
    }

    const nvidia_app_cleaner::CacheScanner scanner;
    const auto initial_results = scanner.scan_all(targets);
    expect(initial_results.size() == targets.size(), "all simulated targets should be scanned");

    std::uintmax_t scanned_bytes = 0;
    for (const auto &result : initial_results) {
        expect(result.state == nvidia_app_cleaner::ScanState::ready,
               "every populated simulated target should be ready");
        scanned_bytes += result.size_bytes;
    }
    expect(scanned_bytes == expected_total_bytes,
           "the simulated scan should report the exact total size");

    const nvidia_app_cleaner::CacheCleaner cleaner(targets);
    std::size_t cleaned_categories = 0;
    for (const auto &target : targets) {
        if (!target.selected_by_default) {
            continue;
        }
        const auto cleanup = cleaner.clean(target.category);
        expect(cleanup.state == nvidia_app_cleaner::CleanupState::cleaned,
               "every recommended simulated target should be cleaned");
        ++cleaned_categories;
    }
    expect(cleaned_categories == 3, "only the three recommended categories should be cleaned");

    const auto final_results = scanner.scan_all(targets);
    for (std::size_t index = 0; index < targets.size(); ++index) {
        const auto expected_state = targets[index].selected_by_default
                                        ? nvidia_app_cleaner::ScanState::empty
                                        : nvidia_app_cleaner::ScanState::ready;
        expect(final_results[index].state == expected_state,
               "optional simulated targets must remain untouched");
        expect(std::filesystem::is_directory(targets[index].path),
               "cleanup must preserve each approved root directory");
    }
    expect(std::filesystem::exists(outside_file),
           "the simulated cleanup must not touch files outside approved targets");
}

void cleans_only_approved_directory_contents() {
    TemporaryDirectory temporary_directory;
    const auto approved = temporary_directory.path() / "approved";
    const auto nested = approved / "nested";
    std::filesystem::create_directories(nested);
    write_file(approved / "one.bin", 4);
    write_file(nested / "two.bin", 7);

    const nvidia_app_cleaner::CacheCleaner cleaner({target_for(approved)});
    const auto result = cleaner.clean(nvidia_app_cleaner::CacheCategory::game_ready_driver);

    expect(result.state == nvidia_app_cleaner::CleanupState::cleaned,
           "an approved cache should be cleaned");
    expect(result.files_removed == 2, "cleanup should report removed files");
    expect(result.bytes_removed == 11, "cleanup should report removed bytes");
    expect(std::filesystem::is_directory(approved), "cleanup must preserve the approved root");
    expect(std::filesystem::is_empty(approved), "the approved root should be empty after cleanup");
}

void cleans_only_the_selected_driver_package() {
    TemporaryDirectory temporary_directory;
    const auto package_root = temporary_directory.path() / "grd";
    const auto selected_package = package_root / "task-selected";
    const auto selected_extracted = package_root / "post-processing" / "task-selected";
    const auto retained_package = package_root / "task-retained";
    const auto retained_extracted = package_root / "post-processing" / "task-retained";
    std::filesystem::create_directories(selected_package);
    std::filesystem::create_directories(selected_extracted);
    std::filesystem::create_directories(retained_package);
    std::filesystem::create_directories(retained_extracted);
    write_file(selected_package / "610.88-driver.exe", 4);
    write_file(selected_extracted / "setup.exe", 7);
    write_file(retained_package / "609.42-driver.exe", 5);
    write_file(retained_extracted / "setup.exe", 6);

    const nvidia_app_cleaner::CacheCleaner cleaner({target_for(package_root)});
    const auto result = cleaner.clean_driver_package(
        {nvidia_app_cleaner::CacheCategory::game_ready_driver, L"task-selected"});

    expect(result.state == nvidia_app_cleaner::CleanupState::cleaned,
           "a selected driver taskId should be cleaned");
    expect(result.bytes_removed == 11 && result.files_removed == 2,
           "selected raw and post-processing files should be counted");
    expect(!std::filesystem::exists(selected_package) &&
               !std::filesystem::exists(selected_extracted),
           "both directories belonging to the selected taskId should be removed");
    expect(std::filesystem::exists(retained_package / "609.42-driver.exe") &&
               std::filesystem::exists(retained_extracted / "setup.exe"),
           "unselected driver package versions must remain untouched");
}

void rejects_unsafe_driver_package_id() {
    TemporaryDirectory temporary_directory;
    const nvidia_app_cleaner::CacheCleaner cleaner({target_for(temporary_directory.path())});
    const auto result =
        cleaner.clean_driver_package({nvidia_app_cleaner::CacheCategory::game_ready_driver, L".."});

    expect(result.state == nvidia_app_cleaner::CleanupState::not_approved,
           "driver package cleanup must reject path traversal identifiers");
}

void refuses_unapproved_category() {
    TemporaryDirectory temporary_directory;
    const nvidia_app_cleaner::CacheCleaner cleaner({target_for(temporary_directory.path())});
    const auto result = cleaner.clean(nvidia_app_cleaner::CacheCategory::installer2);

    expect(result.state == nvidia_app_cleaner::CleanupState::not_approved,
           "cleanup must reject a category outside the approved target list");
}

void refuses_cache_containing_symbolic_link() {
    TemporaryDirectory temporary_directory;
    const auto cache = temporary_directory.path() / "cache";
    const auto external = temporary_directory.path() / "external";
    std::filesystem::create_directories(cache);
    std::filesystem::create_directories(external);
    write_file(cache / "inside.bin", 4);
    write_file(external / "outside.bin", 32);

    std::error_code error;
    std::filesystem::create_directory_symlink(external, cache / "linked", error);
    if (error) {
        std::cout << "Cleanup symbolic-link test skipped: " << error.message() << '\n';
        return;
    }

    const nvidia_app_cleaner::CacheCleaner cleaner({target_for(cache)});
    const auto result = cleaner.clean(nvidia_app_cleaner::CacheCategory::game_ready_driver);

    expect(result.state == nvidia_app_cleaner::CleanupState::unsafe_path,
           "cleanup must refuse a cache containing a symbolic link");
    expect(std::filesystem::exists(cache / "inside.bin"),
           "preflight refusal must occur before deleting normal files");
    expect(std::filesystem::exists(external / "outside.bin"),
           "cleanup must never follow a symbolic link");
}

} // namespace

int main() {
    try {
        scans_nested_files();
        lists_distinct_driver_package_versions();
        groups_driver_files_by_task_id();
        ignores_files_not_nested_under_task_id();
        validates_driver_package_identifiers();
        ignores_versions_outside_driver_package_categories();
        reports_empty_directory();
        reports_missing_directory();
        rejects_non_directory_root();
        skips_symbolic_links();
        builds_expected_default_targets();
        honours_pre_cancelled_scan();
        simulates_recommended_cleanup_workflow();
        cleans_only_approved_directory_contents();
        cleans_only_the_selected_driver_package();
        rejects_unsafe_driver_package_id();
        refuses_unapproved_category();
        refuses_cache_containing_symbolic_link();
    } catch (const std::exception &error) {
        std::cerr << "Test failure: " << error.what() << '\n';
        return 1;
    }

    std::cout << "All cache scanner tests passed\n";
    return 0;
}
