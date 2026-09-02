#include "platform/windows/windows_platform.h"

#include <knownfolders.h>
#include <shellapi.h>
#include <shlobj.h>
#include <tlhelp32.h>

#include <array>
#include <climits>
#include <iomanip>
#include <sstream>
#include <utility>

namespace nvidia_app_cleaner::windows {
namespace {

class OwnedHandle {
  public:
    OwnedHandle() = default;
    explicit OwnedHandle(HANDLE handle) : handle_(handle) {}
    OwnedHandle(const OwnedHandle &) = delete;
    OwnedHandle &operator=(const OwnedHandle &) = delete;
    OwnedHandle(OwnedHandle &&other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}
    OwnedHandle &operator=(OwnedHandle &&other) noexcept {
        if (this != &other) {
            reset();
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }
    ~OwnedHandle() { reset(); }

    [[nodiscard]] HANDLE get() const { return handle_; }

    void reset(HANDLE handle = nullptr) {
        if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_);
        }
        handle_ = handle;
    }

  private:
    HANDLE handle_{nullptr};
};

std::filesystem::path known_folder(REFKNOWNFOLDERID folder_id) {
    PWSTR raw_path = nullptr;
    const HRESULT result = SHGetKnownFolderPath(folder_id, KF_FLAG_DEFAULT, nullptr, &raw_path);
    if (FAILED(result)) {
        return {};
    }

    std::filesystem::path path(raw_path);
    CoTaskMemFree(raw_path);
    return path;
}

std::wstring trim_ascii_space(std::wstring value) {
    const auto is_space = [](wchar_t character) {
        return character == L' ' || character == L'\t' || character == L'\r' || character == L'\n';
    };
    while (!value.empty() && is_space(value.front())) {
        value.erase(value.begin());
    }
    while (!value.empty() && is_space(value.back())) {
        value.pop_back();
    }
    if (value.size() >= 2 && value.front() == L'"' && value.back() == L'"') {
        value = value.substr(1, value.size() - 2);
    }
    return value;
}

} // namespace

std::filesystem::path program_data_path() { return known_folder(FOLDERID_ProgramData); }

std::filesystem::path program_files_path() { return known_folder(FOLDERID_ProgramFiles); }

std::filesystem::path update_framework_root() {
    const auto program_data = program_data_path();
    if (program_data.empty()) {
        return {};
    }
    return program_data / L"NVIDIA Corporation" / L"NVIDIA App" / L"UpdateFramework";
}

std::filesystem::path create_backup_path(DriverChannel channel) {
    const auto local_app_data = known_folder(FOLDERID_LocalAppData);
    if (local_app_data.empty()) {
        return {};
    }

    SYSTEMTIME time{};
    GetLocalTime(&time);
    std::wostringstream name;
    name << std::setfill(L'0') << std::setw(4) << time.wYear << std::setw(2) << time.wMonth
         << std::setw(2) << time.wDay << L'-' << std::setw(2) << time.wHour << std::setw(2)
         << time.wMinute << std::setw(2) << time.wSecond << L'-' << std::setw(3)
         << time.wMilliseconds;

    return local_app_data / L"NvidiaAppCleaner" / L"Backups" / name.str() /
           driver_channel_directory(channel);
}

bool nvidia_app_is_running() {
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return true;
    }

    PROCESSENTRY32W process{.dwSize = sizeof(PROCESSENTRY32W)};
    bool running = false;
    if (Process32FirstW(snapshot, &process) != FALSE) {
        do {
            if (_wcsicmp(process.szExeFile, L"NVIDIA App.exe") == 0) {
                running = true;
                break;
            }
        } while (Process32NextW(snapshot, &process) != FALSE);
    }
    CloseHandle(snapshot);
    return running;
}

std::vector<InstalledDriverInfo> installed_nvidia_drivers() {
    std::vector<InstalledDriverInfo> drivers;

    std::wstring system_directory(MAX_PATH, L'\0');
    const UINT system_directory_length =
        GetSystemDirectoryW(system_directory.data(), static_cast<UINT>(system_directory.size()));
    if (system_directory_length == 0 || system_directory_length >= system_directory.size()) {
        return drivers;
    }
    system_directory.resize(system_directory_length);
    const std::filesystem::path executable =
        std::filesystem::path(system_directory) / L"nvidia-smi.exe";
    if (GetFileAttributesW(executable.c_str()) == INVALID_FILE_ATTRIBUTES) {
        return drivers;
    }

    SECURITY_ATTRIBUTES security{
        .nLength = sizeof(SECURITY_ATTRIBUTES),
        .lpSecurityDescriptor = nullptr,
        .bInheritHandle = TRUE,
    };
    HANDLE raw_read = nullptr;
    HANDLE raw_write = nullptr;
    if (CreatePipe(&raw_read, &raw_write, &security, 0) == FALSE) {
        return drivers;
    }
    OwnedHandle output_read(raw_read);
    OwnedHandle output_write(raw_write);
    if (SetHandleInformation(output_read.get(), HANDLE_FLAG_INHERIT, 0) == FALSE) {
        return drivers;
    }

    STARTUPINFOW startup{.cb = sizeof(STARTUPINFOW)};
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = output_write.get();
    startup.hStdError = output_write.get();
    PROCESS_INFORMATION process{};
    std::wstring command_line = L"\"" + executable.wstring() +
                                L"\" --query-gpu=name,driver_version "
                                L"--format=csv,noheader,nounits";
    if (CreateProcessW(executable.c_str(), command_line.data(), nullptr, nullptr, TRUE,
                       CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process) == FALSE) {
        return drivers;
    }
    OwnedHandle process_handle(process.hProcess);
    OwnedHandle thread_handle(process.hThread);
    output_write.reset();

    const DWORD wait_result = WaitForSingleObject(process_handle.get(), 5000);
    if (wait_result != WAIT_OBJECT_0) {
        if (wait_result == WAIT_TIMEOUT) {
            TerminateProcess(process_handle.get(), ERROR_TIMEOUT);
            WaitForSingleObject(process_handle.get(), 1000);
        }
        return drivers;
    }
    DWORD exit_code = 1;
    if (GetExitCodeProcess(process_handle.get(), &exit_code) == FALSE || exit_code != 0) {
        return drivers;
    }

    std::string output;
    std::array<char, 1024> buffer{};
    DWORD bytes_read = 0;
    while (output.size() < 64 * 1024 &&
           ReadFile(output_read.get(), buffer.data(), static_cast<DWORD>(buffer.size()),
                    &bytes_read, nullptr) != FALSE &&
           bytes_read > 0) {
        output.append(buffer.data(), bytes_read);
    }

    std::istringstream lines(output);
    std::string line;
    while (std::getline(lines, line)) {
        const auto comma = line.rfind(',');
        if (comma == std::string::npos) {
            continue;
        }
        const auto gpu_name = utf8_to_wide(line.substr(0, comma));
        const auto driver_version = utf8_to_wide(line.substr(comma + 1));
        if (!gpu_name || !driver_version) {
            continue;
        }
        std::wstring trimmed_name = trim_ascii_space(*gpu_name);
        std::wstring trimmed_version = trim_ascii_space(*driver_version);
        if (!trimmed_name.empty() && !trimmed_version.empty()) {
            drivers.push_back({std::move(trimmed_name), std::move(trimmed_version)});
        }
    }
    return drivers;
}

std::optional<std::wstring> utf8_to_wide(std::string_view text) {
    if (text.empty()) {
        return std::wstring{};
    }
    if (text.size() > static_cast<std::size_t>(INT_MAX)) {
        return std::nullopt;
    }

    const int input_size = static_cast<int>(text.size());
    const int output_size =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), input_size, nullptr, 0);
    if (output_size <= 0) {
        return std::nullopt;
    }

    std::wstring result(static_cast<std::size_t>(output_size), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), input_size, result.data(),
                            output_size) != output_size) {
        return std::nullopt;
    }
    return result;
}

std::wstring format_windows_error(std::uint32_t error_code) {
    wchar_t *buffer = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, error_code, 0, reinterpret_cast<wchar_t *>(&buffer), 0, nullptr);
    if (length == 0 || buffer == nullptr) {
        return {};
    }

    std::wstring message(buffer, length);
    LocalFree(buffer);
    while (!message.empty() &&
           (message.back() == L'\r' || message.back() == L'\n' || message.back() == L' ')) {
        message.pop_back();
    }
    return message;
}

ElevatedLaunchResult launch_current_executable_elevated(HWND owner, std::wstring_view parameters,
                                                        bool wait_for_exit,
                                                        std::uint32_t &error_code) {
    error_code = ERROR_SUCCESS;
    std::wstring executable(32768, L'\0');
    const DWORD length =
        GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    if (length == 0 || static_cast<std::size_t>(length) >= executable.size()) {
        error_code = length == 0 ? GetLastError() : ERROR_INSUFFICIENT_BUFFER;
        return ElevatedLaunchResult::failed;
    }
    executable.resize(length);

    const std::wstring owned_parameters(parameters);
    SHELLEXECUTEINFOW launch{
        .cbSize = sizeof(SHELLEXECUTEINFOW),
        .fMask =
            static_cast<ULONG>(SEE_MASK_NOASYNC | (wait_for_exit ? SEE_MASK_NOCLOSEPROCESS : 0)),
        .hwnd = owner,
        .lpVerb = L"runas",
        .lpFile = executable.c_str(),
        .lpParameters = owned_parameters.empty() ? nullptr : owned_parameters.c_str(),
        .nShow = SW_SHOWNORMAL,
    };
    if (ShellExecuteExW(&launch) == FALSE) {
        error_code = GetLastError();
        return error_code == ERROR_CANCELLED ? ElevatedLaunchResult::cancelled
                                             : ElevatedLaunchResult::failed;
    }

    if (!wait_for_exit) {
        return ElevatedLaunchResult::launched;
    }
    if (launch.hProcess == nullptr) {
        error_code = ERROR_INVALID_HANDLE;
        return ElevatedLaunchResult::failed;
    }

    const DWORD wait_result = WaitForSingleObject(launch.hProcess, INFINITE);
    const DWORD wait_error = wait_result == WAIT_FAILED ? GetLastError() : ERROR_SUCCESS;
    CloseHandle(launch.hProcess);
    if (wait_result != WAIT_OBJECT_0) {
        error_code = wait_error != ERROR_SUCCESS ? wait_error : ERROR_GEN_FAILURE;
        return ElevatedLaunchResult::failed;
    }
    return ElevatedLaunchResult::launched;
}

NvidiaUpdateServicePause::~NvidiaUpdateServicePause() {
    if (restart_required_) {
        static_cast<void>(resume());
    }
    if (service_ != nullptr) {
        CloseServiceHandle(service_);
    }
    if (manager_ != nullptr) {
        CloseServiceHandle(manager_);
    }
}

ServiceOperationResult NvidiaUpdateServicePause::pause() {
    manager_ = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (manager_ == nullptr) {
        return {ServiceErrorKind::windows_error, GetLastError()};
    }

    service_ = OpenServiceW(manager_, L"NvContainerLocalSystem",
                            SERVICE_QUERY_STATUS | SERVICE_STOP | SERVICE_START);
    if (service_ == nullptr) {
        return {ServiceErrorKind::windows_error, GetLastError()};
    }

    SERVICE_STATUS_PROCESS status{};
    if (const auto result = query(status); !result) {
        return result;
    }
    if (status.dwCurrentState == SERVICE_STOPPED) {
        return {};
    }
    if (status.dwCurrentState != SERVICE_RUNNING) {
        return {ServiceErrorKind::invalid_state, ERROR_SUCCESS};
    }

    SERVICE_STATUS ignored_status{};
    if (ControlService(service_, SERVICE_CONTROL_STOP, &ignored_status) == FALSE) {
        return {ServiceErrorKind::windows_error, GetLastError()};
    }
    restart_required_ = true;
    return wait_for_state(SERVICE_STOPPED);
}

ServiceOperationResult NvidiaUpdateServicePause::resume() {
    if (!restart_required_) {
        return {};
    }
    if (StartServiceW(service_, 0, nullptr) == FALSE &&
        GetLastError() != ERROR_SERVICE_ALREADY_RUNNING) {
        return {ServiceErrorKind::windows_error, GetLastError()};
    }
    if (const auto result = wait_for_state(SERVICE_RUNNING); !result) {
        return result;
    }
    restart_required_ = false;
    return {};
}

ServiceOperationResult NvidiaUpdateServicePause::query(SERVICE_STATUS_PROCESS &status) const {
    DWORD bytes_needed = 0;
    if (QueryServiceStatusEx(service_, SC_STATUS_PROCESS_INFO, reinterpret_cast<BYTE *>(&status),
                             sizeof(status), &bytes_needed) == FALSE) {
        return {ServiceErrorKind::windows_error, GetLastError()};
    }
    return {};
}

ServiceOperationResult
NvidiaUpdateServicePause::wait_for_state(std::uint32_t expected_state) const {
    for (int attempt = 0; attempt < 75; ++attempt) {
        SERVICE_STATUS_PROCESS status{};
        if (const auto result = query(status); !result) {
            return result;
        }
        if (status.dwCurrentState == expected_state) {
            return {};
        }
        Sleep(200);
    }
    return {ServiceErrorKind::timeout, ERROR_SUCCESS};
}

} // namespace nvidia_app_cleaner::windows
