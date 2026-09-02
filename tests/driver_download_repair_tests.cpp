#include "nvidia_app_cleaner/cache_cleaner.h"
#include "nvidia_app_cleaner/driver_download_repair.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

class TemporaryDirectory {
  public:
    TemporaryDirectory() {
        const auto unique_value = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
                ("nvidia_app_cleaner_repair_tests_" + std::to_string(unique_value));
        std::filesystem::create_directories(path_);
    }

    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] const std::filesystem::path &path() const { return path_; }

  private:
    std::filesystem::path path_;
};

void expect(bool condition, const std::string &message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void write_text(const std::filesystem::path &path, const std::string &content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output << content;
    if (!output) {
        throw std::runtime_error("test file could not be written");
    }
}

std::string read_text(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    std::ostringstream output;
    output << input.rdbuf();
    return output.str();
}

std::string json_escape(const std::string &value) {
    std::string escaped;
    for (const char character : value) {
        if (character == '\\' || character == '"') {
            escaped.push_back('\\');
        }
        escaped.push_back(character);
    }
    return escaped;
}

std::string completed_object(const std::string &location, const std::string &task_id,
                             const std::string &version) {
    return "{\"state\":8,\"percentComplete\":100.0,\"fileLocation\":\"" + json_escape(location) +
           "\",\"taskId\":\"" + json_escape(task_id) + "\",\"version\":\"" + json_escape(version) +
           "\"}";
}

std::string completed_record(const std::string &location) {
    return "[" + completed_object(location, "task-1", "610.88") + "]";
}

std::filesystem::path download_status(
    const std::filesystem::path &root,
    nvidia_app_cleaner::DriverChannel channel = nvidia_app_cleaner::DriverChannel::game_ready) {
    return root / "status" / nvidia_app_cleaner::driver_channel_directory(channel) /
           "download.json";
}

void reports_valid_completed_download() {
    TemporaryDirectory temporary;
    const auto root = temporary.path() / "framework";
    const auto installer = root / "ota-artifacts" / "grd" / "task-1" / "driver.exe";
    write_text(installer, "installer");
    write_text(download_status(root), completed_record(installer.string()));

    const nvidia_app_cleaner::DriverDownloadRepair repair;
    const auto assessment = repair.assess(root, nvidia_app_cleaner::DriverChannel::game_ready);

    expect(assessment.state == nvidia_app_cleaner::DriverDownloadState::ready_to_install,
           "an existing completed installer must not be repairable");
}

void discards_valid_completed_download_for_cleanup() {
    TemporaryDirectory temporary;
    const auto root = temporary.path() / "framework";
    const auto installer = root / "ota-artifacts" / "grd" / "task-1" / "driver.exe";
    write_text(installer, "installer");
    write_text(download_status(root), completed_record(installer.string()));

    const nvidia_app_cleaner::DriverDownloadRepair repair;
    const auto backup = temporary.path() / "backup";
    const auto result =
        repair.discard_completed_download(root, nvidia_app_cleaner::DriverChannel::game_ready);

    expect(result.repaired, "cleanup should be able to discard a completed valid download");
    expect(read_text(download_status(root)) == "[]\n",
           "discarding a download should remove its taskId record");
    expect(!std::filesystem::exists(backup), "cleanup must not create a persistent backup");
    expect(std::filesystem::exists(installer),
           "discarding status must not delete the installer itself");
}

void discards_only_the_requested_completed_download() {
    TemporaryDirectory temporary;
    const auto root = temporary.path() / "framework";
    const auto first_installer = root / "ota-artifacts" / "grd" / "task-first" / "driver.exe";
    const auto second_installer = root / "ota-artifacts" / "grd" / "task-second" / "driver.exe";
    write_text(first_installer, "first installer");
    write_text(second_installer, "second installer");
    const auto first_record = completed_object(first_installer.string(), "task-first", "610.88");
    const auto second_record = completed_object(second_installer.string(), "task-second", "609.42");
    write_text(download_status(root), "[" + first_record + "," + second_record + "]");
    write_text(root / "status" / "grd" / "postprocessing.json",
               "[{\"taskId\":\"task-first\"},{\"taskId\":\"task-second\"}]");

    const nvidia_app_cleaner::DriverDownloadRepair repair;
    const auto assessment =
        repair.assess_task(root, nvidia_app_cleaner::DriverChannel::game_ready, "task-first");
    expect(assessment.state == nvidia_app_cleaner::DriverDownloadState::ready_to_install &&
               assessment.version == "610.88",
           "taskId assessment should return the requested completed download");

    const auto result = repair.discard_completed_download_task(
        root, nvidia_app_cleaner::DriverChannel::game_ready, "task-first");
    expect(result.repaired, "the requested completed download record should be removable");
    expect(read_text(download_status(root)) == "[" + second_record + "]\n",
           "unselected download records must remain in download.json");
    expect(read_text(root / "status" / "grd" / "postprocessing.json") ==
               "[{\"taskId\":\"task-second\"}]\n",
           "only the matching post-processing record should be removed");
    expect(std::filesystem::exists(first_installer) && std::filesystem::exists(second_installer),
           "status cleanup must not delete either package directory itself");
}

void simulates_driver_package_cleanup_without_stale_status() {
    TemporaryDirectory temporary;
    const auto root = temporary.path() / "framework";
    const auto package_root = root / "ota-artifacts" / "grd";
    const auto installer = package_root / "task-1" / "driver.exe";
    write_text(installer, "simulated driver installer");
    write_text(download_status(root), completed_record(installer.string()));

    const nvidia_app_cleaner::DriverDownloadRepair repair;
    const auto backup = temporary.path() / "backup";
    const auto discard =
        repair.discard_completed_download(root, nvidia_app_cleaner::DriverChannel::game_ready);
    expect(discard.repaired,
           "simulated cleanup must remove completed status before deleting its package");

    const nvidia_app_cleaner::ScanTarget package_target{
        nvidia_app_cleaner::CacheCategory::game_ready_driver,
        L"Simulated Game Ready package",
        package_root,
        false,
    };
    const nvidia_app_cleaner::CacheCleaner cleaner({package_target});
    const auto cleanup = cleaner.clean(nvidia_app_cleaner::CacheCategory::game_ready_driver);
    expect(cleanup.state == nvidia_app_cleaner::CleanupState::cleaned,
           "the simulated driver package should be deleted after status cleanup");
    expect(!std::filesystem::exists(installer),
           "the simulated installer should be removed by package cleanup");
    expect(std::filesystem::is_directory(package_root),
           "the approved package root should be preserved");
    expect(read_text(download_status(root)) == "[]\n",
           "package cleanup must not leave a completed download record");
    expect(!std::filesystem::exists(backup), "driver cleanup must not leave a backup directory");

    const auto final_assessment =
        repair.assess(root, nvidia_app_cleaner::DriverChannel::game_ready);
    expect(final_assessment.state == nvidia_app_cleaner::DriverDownloadState::no_record,
           "the simulated NVIDIA App state must not be stuck at 100 percent");
}

void discards_completed_nvidia_app_download_for_cleanup() {
    TemporaryDirectory temporary;
    const auto root = temporary.path() / "framework";
    const auto installer = root / "ota-artifacts" / "nvapp" / "task-1" / "nvapp.exe";
    write_text(installer, "installer");
    write_text(download_status(root, nvidia_app_cleaner::DriverChannel::nvidia_app),
               completed_record(installer.string()));

    const nvidia_app_cleaner::DriverDownloadRepair repair;
    const auto backup = temporary.path() / "backup";
    const auto result =
        repair.discard_completed_download(root, nvidia_app_cleaner::DriverChannel::nvidia_app);

    expect(result.repaired, "cleanup should discard a completed NVIDIA App download");
    expect(read_text(download_status(root, nvidia_app_cleaner::DriverChannel::nvidia_app)) ==
               "[]\n",
           "discarding an NVIDIA App download should remove its taskId record");
    expect(!std::filesystem::exists(backup),
           "NVIDIA App cleanup must not create a persistent backup");
    expect(std::filesystem::exists(installer),
           "discarding NVIDIA App status must not delete its installer itself");
}

void repairs_completed_download_with_missing_installer() {
    TemporaryDirectory temporary;
    const auto root = temporary.path() / "framework";
    const auto missing_installer = root / "ota-artifacts" / "grd" / "task-1" / "missing-driver.exe";
    const auto original_download = completed_record(missing_installer.string());
    const std::string original_postprocessing =
        "[{\"taskId\":\"task-1\",\"postProcessingInput\":\"missing-driver.exe\"}]";
    write_text(download_status(root), original_download);
    write_text(root / "status" / "grd" / "postprocessing.json", original_postprocessing);

    const nvidia_app_cleaner::DriverDownloadRepair repair;
    const auto assessment = repair.assess(root, nvidia_app_cleaner::DriverChannel::game_ready);
    expect(assessment.state == nvidia_app_cleaner::DriverDownloadState::broken_file_location,
           "a completed record whose installer is missing should be repairable");

    const auto backup = temporary.path() / "backup";
    const auto result = repair.repair(root, nvidia_app_cleaner::DriverChannel::game_ready, backup);
    expect(result.repaired, "repair should reset the stale record");
    expect(read_text(download_status(root)) == "[]\n", "download record should be reset");
    expect(read_text(root / "status" / "grd" / "postprocessing.json") == "[]\n",
           "matching post-processing state should be reset");
    expect(read_text(backup / "download.json") == original_download,
           "download record should be backed up before repair");
    expect(read_text(backup / "postprocessing.json") == original_postprocessing,
           "post-processing state should be backed up before repair");
}

void repairs_empty_file_location() {
    TemporaryDirectory temporary;
    const auto root = temporary.path() / "framework";
    write_text(download_status(root), completed_record(""));

    const nvidia_app_cleaner::DriverDownloadRepair repair;
    const auto assessment = repair.assess(root, nvidia_app_cleaner::DriverChannel::game_ready);

    expect(assessment.state == nvidia_app_cleaner::DriverDownloadState::broken_file_location,
           "an empty fileLocation should be repairable when download is complete");
}

void removes_only_the_broken_record() {
    TemporaryDirectory temporary;
    const auto root = temporary.path() / "framework";
    const auto valid_installer = root / "ota-artifacts" / "grd" / "task-valid" / "valid-driver.exe";
    const auto missing_installer =
        root / "ota-artifacts" / "grd" / "task-broken" / "missing-driver.exe";
    write_text(valid_installer, "installer");

    const auto valid_download = completed_object(valid_installer.string(), "task-valid", "609.99");
    const auto broken_download =
        completed_object(missing_installer.string(), "task-broken", "610.88");
    const std::string original_download = "[" + valid_download + "," + broken_download + "]";
    const std::string valid_postprocessing =
        "{\"taskId\":\"task-valid\",\"postProcessingInput\":\"valid-driver.exe\"}";
    const std::string broken_postprocessing =
        "{\"taskId\":\"task-broken\",\"postProcessingInput\":\"missing-driver.exe\"}";
    const std::string original_postprocessing =
        "[" + valid_postprocessing + "," + broken_postprocessing + "]";
    write_text(download_status(root), original_download);
    write_text(root / "status" / "grd" / "postprocessing.json", original_postprocessing);

    const nvidia_app_cleaner::DriverDownloadRepair repair;
    const auto assessment = repair.assess(root, nvidia_app_cleaner::DriverChannel::game_ready);
    expect(assessment.state == nvidia_app_cleaner::DriverDownloadState::broken_file_location,
           "the broken record should be found after a valid record");
    expect(assessment.task_id == "task-broken", "repair should target the broken taskId");

    const auto backup = temporary.path() / "backup";
    const auto result = repair.repair(root, nvidia_app_cleaner::DriverChannel::game_ready, backup);
    expect(result.repaired, "the broken record should be removed");
    expect(read_text(download_status(root)) == "[" + valid_download + "]\n",
           "the valid download record must be preserved");
    expect(read_text(root / "status" / "grd" / "postprocessing.json") ==
               "[" + valid_postprocessing + "]\n",
           "the valid post-processing record must be preserved");
    expect(read_text(backup / "download.json") == original_download,
           "the complete original download array should be backed up");
    expect(read_text(backup / "postprocessing.json") == original_postprocessing,
           "the complete original post-processing array should be backed up");
}

void refuses_incomplete_download() {
    TemporaryDirectory temporary;
    const auto root = temporary.path() / "framework";
    const std::string incomplete =
        "[{\"state\":2,\"percentComplete\":45.0,\"fileLocation\":\"missing.exe\","
        "\"taskId\":\"task-1\"}]";
    write_text(download_status(root), incomplete);

    const nvidia_app_cleaner::DriverDownloadRepair repair;
    const auto assessment = repair.assess(root, nvidia_app_cleaner::DriverChannel::game_ready);
    expect(assessment.state == nvidia_app_cleaner::DriverDownloadState::downloading,
           "an incomplete download must not be repairable");

    const auto result = repair.repair(root, nvidia_app_cleaner::DriverChannel::game_ready,
                                      temporary.path() / "backup");
    expect(!result.repaired, "repair must refuse an incomplete download");
    expect(read_text(download_status(root)) == incomplete,
           "a refused repair must not modify the status file");
}

void refuses_unknown_json_shape() {
    TemporaryDirectory temporary;
    const auto root = temporary.path() / "framework";
    write_text(download_status(root), "{\"state\":8}");

    const nvidia_app_cleaner::DriverDownloadRepair repair;
    const auto assessment = repair.assess(root, nvidia_app_cleaner::DriverChannel::game_ready);

    expect(assessment.state == nvidia_app_cleaner::DriverDownloadState::unsupported_record,
           "unknown JSON formats must not be modified");
}

} // namespace

int main() {
    try {
        reports_valid_completed_download();
        discards_valid_completed_download_for_cleanup();
        discards_only_the_requested_completed_download();
        simulates_driver_package_cleanup_without_stale_status();
        discards_completed_nvidia_app_download_for_cleanup();
        repairs_completed_download_with_missing_installer();
        repairs_empty_file_location();
        removes_only_the_broken_record();
        refuses_incomplete_download();
        refuses_unknown_json_shape();
    } catch (const std::exception &error) {
        std::cerr << "Test failure: " << error.what() << '\n';
        return 1;
    }

    std::cout << "All driver download repair tests passed\n";
    return 0;
}
