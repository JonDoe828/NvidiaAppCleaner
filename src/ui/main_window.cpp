#include "ui/main_window.h"

#include "app/resource.h"
#include "nvidia_app_cleaner/app_settings.h"
#include "nvidia_app_cleaner/cache_cleaner.h"
#include "nvidia_app_cleaner/cache_scanner.h"
#include "nvidia_app_cleaner/driver_download_repair.h"
#include "nvidia_app_cleaner/localization.h"
#include "nvidia_app_cleaner/startup_options.h"
#include "platform/windows/windows_platform.h"
#include "ui/theme_manager.h"

#include <windows.h>

#include <commctrl.h>
#include <shellapi.h>

#include <algorithm>
#include <array>
#include <exception>
#include <iomanip>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

constexpr wchar_t kWindowClassName[] = L"NvidiaAppCleanerMainWindow";
constexpr int kScanButtonId = IDM_ACTION_SCAN;
constexpr int kRepairButtonId = IDM_ACTION_REPAIR;
constexpr int kSettingsButtonId = IDM_VIEW_SETTINGS;
constexpr int kSettingsSaveButtonId = 1005;
constexpr int kCleanButtonId = IDM_ACTION_CLEAN;
constexpr int kSelectRecommendedButtonId = IDM_ACTION_SELECT_RECOMMENDED;
constexpr int kClearSelectionButtonId = IDM_ACTION_CLEAR_SELECTION;
constexpr int kCleanupNavigationButtonId = IDM_VIEW_CLEANUP;
constexpr int kRepairNavigationButtonId = IDM_VIEW_REPAIR;
constexpr UINT kSystemThemeChangedMessage = WM_APP + 1;
constexpr UINT kScanCompletedMessage = WM_APP + 2;
constexpr int kMinimumCategoryColumnWidthDip = 210;
constexpr int kMinimumLocationColumnWidthDip = 240;
constexpr int kMinimumSizeColumnWidthDip = 80;
constexpr int kMinimumStatusColumnWidthDip = 110;
constexpr wchar_t kRepositoryUrl[] = L"https://github.com/JonDoe828/NvidiaAppCleaner";

HWND g_list_view = nullptr;
HWND g_heading_label = nullptr;
HWND g_driver_info_label = nullptr;
HWND g_summary_label = nullptr;
HWND g_scan_button = nullptr;
HWND g_clean_button = nullptr;
HWND g_repair_button = nullptr;
HWND g_select_recommended_button = nullptr;
HWND g_clear_selection_button = nullptr;
HWND g_status_label = nullptr;
HWND g_settings_title_label = nullptr;
HWND g_settings_description_label = nullptr;
HWND g_always_admin_checkbox = nullptr;
HWND g_settings_status_label = nullptr;
HWND g_settings_save_button = nullptr;
HWND g_repair_title_label = nullptr;
HWND g_repair_description_label = nullptr;
HWND g_repair_status_label = nullptr;
HMENU g_main_menu = nullptr;
HBRUSH g_menu_background_brush = nullptr;

struct MenuItemVisual {
    std::wstring text;
    bool top_level{false};
    bool has_submenu{false};
};

std::vector<std::unique_ptr<MenuItemVisual>> g_menu_item_visuals;

enum class Page {
    cleanup,
    repair,
    settings,
};

Page g_current_page = Page::cleanup;
bool g_refreshing_results = false;
bool g_scan_in_progress = false;
bool g_resizing_list_columns = false;
int g_tracked_list_column = -1;
int g_tracked_column_pair_width = 0;
bool g_list_columns_initialized = false;
UINT g_list_columns_dpi = USER_DEFAULT_SCREEN_DPI;
std::size_t g_repairable_count = 0;
std::vector<nvidia_app_cleaner::ScanResult> g_scan_results;
std::vector<nvidia_app_cleaner::windows::InstalledDriverInfo> g_installed_drivers;

using DriverPackageSelectionKey = std::pair<nvidia_app_cleaner::CacheCategory, std::wstring>;

struct CleanupSelectionState {
    std::map<nvidia_app_cleaner::CacheCategory, bool> categories;
    std::map<DriverPackageSelectionKey, bool> driver_packages;
};

enum class CleanupItemKind {
    category,
    driver_package,
    rollback_parent,
    optional_parent,
};

struct DisplayedCleanupItem {
    CleanupItemKind kind{CleanupItemKind::category};
    std::size_t scan_result_index{0};
    std::optional<std::size_t> driver_package_index;
};

struct SelectedCleanupItems {
    std::vector<nvidia_app_cleaner::CacheCategory> categories;
    std::vector<nvidia_app_cleaner::DriverPackageId> driver_packages;
};

std::vector<DisplayedCleanupItem> g_displayed_cleanup_items;
CleanupSelectionState g_scan_previous_selection;
bool g_rollback_drivers_expanded = false;
bool g_optional_cleanup_expanded = false;
nvidia_app_cleaner::UiLanguage g_language = nvidia_app_cleaner::UiLanguage::english;
nvidia_app_cleaner::ui::ThemeManager g_theme;
HFONT g_ui_font = nullptr;
HFONT g_heading_font = nullptr;
HFONT g_summary_font = nullptr;
HFONT g_primary_font = nullptr;

struct ScanCompletion {
    std::vector<nvidia_app_cleaner::ScanResult> results;
    std::vector<nvidia_app_cleaner::windows::InstalledDriverInfo> installed_drivers;
    std::size_t repairable_count{0};
    bool failed{false};
};

std::jthread g_scan_thread;
std::mutex g_scan_mutex;
std::optional<ScanCompletion> g_pending_scan;

enum class SettingsStatus {
    none,
    saved,
    read_failed,
    write_failed,
};

SettingsStatus g_settings_status = SettingsStatus::none;
DWORD g_settings_error = ERROR_SUCCESS;

const wchar_t *tr(nvidia_app_cleaner::TextId id) {
    return nvidia_app_cleaner::text(g_language, id);
}

HRESULT CALLBACK about_dialog_callback(HWND dialog, UINT notification, WPARAM, LPARAM data,
                                       LONG_PTR) {
    if (notification == TDN_HYPERLINK_CLICKED && data != 0) {
        const auto *url = reinterpret_cast<const wchar_t *>(data);
        if (lstrcmpW(url, kRepositoryUrl) == 0) {
            static_cast<void>(
                ShellExecuteW(dialog, L"open", kRepositoryUrl, nullptr, nullptr, SW_SHOWNORMAL));
        }
    }
    return S_OK;
}

void show_about_dialog(HWND owner) {
    const TASKDIALOGCONFIG dialog{
        .cbSize = sizeof(TASKDIALOGCONFIG),
        .hwndParent = owner,
        .hInstance = GetModuleHandleW(nullptr),
        .dwFlags = TDF_ENABLE_HYPERLINKS | TDF_POSITION_RELATIVE_TO_WINDOW | TDF_SIZE_TO_CONTENT,
        .dwCommonButtons = TDCBF_OK_BUTTON,
        .pszWindowTitle = tr(nvidia_app_cleaner::TextId::window_title),
        .pszMainInstruction = tr(nvidia_app_cleaner::TextId::window_title),
        .pszContent = tr(nvidia_app_cleaner::TextId::about_content),
        .pfCallback = about_dialog_callback,
    };

    if (FAILED(TaskDialogIndirect(&dialog, nullptr, nullptr, nullptr))) {
        MessageBoxW(owner, kRepositoryUrl, tr(nvidia_app_cleaner::TextId::window_title),
                    MB_OK | MB_ICONINFORMATION);
    }
}

const wchar_t *channel_name(nvidia_app_cleaner::DriverChannel channel) {
    switch (channel) {
    case nvidia_app_cleaner::DriverChannel::game_ready:
        return tr(nvidia_app_cleaner::TextId::channel_game_ready);
    case nvidia_app_cleaner::DriverChannel::studio:
        return tr(nvidia_app_cleaner::TextId::channel_studio);
    case nvidia_app_cleaner::DriverChannel::nvidia_app:
        return tr(nvidia_app_cleaner::TextId::category_app_update);
    }
    return tr(nvidia_app_cleaner::TextId::state_unknown);
}

std::wstring utf8_to_wide(const std::string &text) {
    const auto converted = nvidia_app_cleaner::windows::utf8_to_wide(text);
    return converted.value_or(tr(nvidia_app_cleaner::TextId::unknown_error));
}

std::wstring windows_error_message(DWORD error) {
    const std::wstring message = nvidia_app_cleaner::windows::format_windows_error(error);
    if (message.empty()) {
        return tr(nvidia_app_cleaner::TextId::windows_error_prefix) + std::to_wstring(error);
    }
    return message;
}

std::wstring
service_error_message(const nvidia_app_cleaner::windows::ServiceOperationResult &result) {
    using nvidia_app_cleaner::windows::ServiceErrorKind;
    switch (result.error) {
    case ServiceErrorKind::none:
        return {};
    case ServiceErrorKind::windows_error:
        return windows_error_message(result.windows_error);
    case ServiceErrorKind::invalid_state:
        return tr(nvidia_app_cleaner::TextId::service_invalid_state);
    case ServiceErrorKind::timeout:
        return tr(nvidia_app_cleaner::TextId::service_timeout);
    }
    return tr(nvidia_app_cleaner::TextId::unknown_error);
}

std::wstring format_size(std::uintmax_t bytes) {
    constexpr double kUnit = 1024.0;
    const wchar_t *suffix = L"B";
    double value = static_cast<double>(bytes);

    if (value >= kUnit) {
        value /= kUnit;
        suffix = L"KB";
    }
    if (value >= kUnit) {
        value /= kUnit;
        suffix = L"MB";
    }
    if (value >= kUnit) {
        value /= kUnit;
        suffix = L"GB";
    }

    std::wostringstream output;
    output << std::fixed << std::setprecision(value >= 10.0 ? 1 : 2) << value << L' ' << suffix;
    return output.str();
}

const wchar_t *state_text(nvidia_app_cleaner::ScanState state) {
    using nvidia_app_cleaner::ScanState;
    switch (state) {
    case ScanState::ready:
        return tr(nvidia_app_cleaner::TextId::state_found);
    case ScanState::empty:
        return tr(nvidia_app_cleaner::TextId::state_empty);
    case ScanState::not_found:
        return tr(nvidia_app_cleaner::TextId::state_not_found);
    case ScanState::inaccessible:
        return tr(nvidia_app_cleaner::TextId::state_access_error);
    case ScanState::unsafe_path:
        return tr(nvidia_app_cleaner::TextId::state_unsafe_path);
    case ScanState::partial:
        return tr(nvidia_app_cleaner::TextId::state_partial);
    }
    return tr(nvidia_app_cleaner::TextId::state_unknown);
}

const wchar_t *category_text(nvidia_app_cleaner::CacheCategory category) {
    using nvidia_app_cleaner::CacheCategory;
    switch (category) {
    case CacheCategory::game_ready_driver:
        return tr(nvidia_app_cleaner::TextId::category_game_ready);
    case CacheCategory::studio_driver:
        return tr(nvidia_app_cleaner::TextId::category_studio);
    case CacheCategory::nvidia_app_update:
        return tr(nvidia_app_cleaner::TextId::category_app_update);
    case CacheCategory::legacy_update_cache:
        return tr(nvidia_app_cleaner::TextId::category_legacy_update);
    case CacheCategory::legacy_downloader:
        return tr(nvidia_app_cleaner::TextId::category_legacy_downloader);
    case CacheCategory::installer2:
        return tr(nvidia_app_cleaner::TextId::category_installer2);
    case CacheCategory::ngx_models:
        return tr(nvidia_app_cleaner::TextId::category_ngx_models);
    }
    return tr(nvidia_app_cleaner::TextId::state_unknown);
}

bool is_driver_category(nvidia_app_cleaner::CacheCategory category);

void set_subitem(int row, int column, const std::wstring &text) {
    ListView_SetItemText(g_list_view, row, column, const_cast<wchar_t *>(text.c_str()));
}

std::wstring joined_package_versions(const nvidia_app_cleaner::ScanResult &result) {
    std::wstring versions;
    for (const auto &version : result.package_versions) {
        if (!versions.empty()) {
            versions += L", ";
        }
        versions += version;
    }
    return versions;
}

std::wstring driver_package_name(const nvidia_app_cleaner::DriverPackageScanResult &package) {
    const auto channel = package.id.category == nvidia_app_cleaner::CacheCategory::studio_driver
                             ? nvidia_app_cleaner::DriverChannel::studio
                             : nvidia_app_cleaner::DriverChannel::game_ready;
    std::wstring name = channel_name(channel);
    name += L"  ";
    name +=
        package.version.empty() ? tr(nvidia_app_cleaner::TextId::state_unknown) : package.version;
    return name;
}

std::filesystem::path
driver_package_display_path(const nvidia_app_cleaner::DriverPackageScanResult &package) {
    return package.installer_path.empty() ? package.package_path : package.installer_path;
}

void update_installed_driver_text() {
    if (g_scan_in_progress) {
        SetWindowTextW(g_driver_info_label,
                       tr(nvidia_app_cleaner::TextId::current_driver_scanning));
        return;
    }
    if (g_installed_drivers.empty()) {
        SetWindowTextW(g_driver_info_label,
                       tr(nvidia_app_cleaner::TextId::current_driver_not_detected));
        return;
    }

    std::wstring label = tr(nvidia_app_cleaner::TextId::current_driver_prefix);
    bool first_driver = true;
    for (const auto &driver : g_installed_drivers) {
        if (!first_driver) {
            label += L";  ";
        }
        label += driver.gpu_name + L"  ·  " + driver.driver_version;
        first_driver = false;
    }
    SetWindowTextW(g_driver_info_label, label.c_str());
}

bool is_cleanable(const nvidia_app_cleaner::ScanResult &result) {
    return result.state == nvidia_app_cleaner::ScanState::ready && result.file_count > 0;
}

bool is_cleanable(const nvidia_app_cleaner::DriverPackageScanResult &package) {
    return package.file_count > 0;
}

const nvidia_app_cleaner::ScanResult &scan_result_for(const DisplayedCleanupItem &item) {
    return g_scan_results[item.scan_result_index];
}

bool is_parent_item(const DisplayedCleanupItem &item) {
    return item.kind == CleanupItemKind::rollback_parent ||
           item.kind == CleanupItemKind::optional_parent;
}

const nvidia_app_cleaner::DriverPackageScanResult *
driver_package_for(const DisplayedCleanupItem &item) {
    if (!item.driver_package_index) {
        return nullptr;
    }
    return &g_scan_results[item.scan_result_index].driver_packages[*item.driver_package_index];
}

CleanupSelectionState current_selection();

SelectedCleanupItems selected_cleanup_items() {
    g_scan_previous_selection = current_selection();
    SelectedCleanupItems selected;
    for (const auto &result : g_scan_results) {
        if (is_driver_category(result.target.category)) {
            for (const auto &package : result.driver_packages) {
                const auto selection = g_scan_previous_selection.driver_packages.find(
                    DriverPackageSelectionKey{package.id.category, package.id.task_id});
                if (is_cleanable(package) &&
                    selection != g_scan_previous_selection.driver_packages.end() &&
                    selection->second) {
                    selected.driver_packages.push_back(package.id);
                }
            }
            continue;
        }
        const auto selection = g_scan_previous_selection.categories.find(result.target.category);
        if (is_cleanable(result) && selection != g_scan_previous_selection.categories.end() &&
            selection->second) {
            selected.categories.push_back(result.target.category);
        }
    }
    return selected;
}

const nvidia_app_cleaner::DriverPackageScanResult *
find_scanned_driver_package(const nvidia_app_cleaner::DriverPackageId &id) {
    for (const auto &result : g_scan_results) {
        const auto package =
            std::find_if(result.driver_packages.begin(), result.driver_packages.end(),
                         [&](const auto &candidate) { return candidate.id == id; });
        if (package != result.driver_packages.end()) {
            return &*package;
        }
    }
    return nullptr;
}

CleanupSelectionState current_selection() {
    CleanupSelectionState selection = g_scan_previous_selection;
    for (std::size_t row = 0; row < g_displayed_cleanup_items.size(); ++row) {
        const auto &displayed = g_displayed_cleanup_items[row];
        if (is_parent_item(displayed)) {
            continue;
        }
        const auto &result = scan_result_for(displayed);
        const bool selected = ListView_GetCheckState(g_list_view, static_cast<int>(row)) != FALSE;
        if (const auto *package = driver_package_for(displayed); package != nullptr) {
            selection.driver_packages[DriverPackageSelectionKey{package->id.category,
                                                                package->id.task_id}] = selected;
        } else {
            selection.categories[result.target.category] = selected;
        }
    }
    return selection;
}

bool all_driver_packages_selected(const CleanupSelectionState &selection) {
    bool found = false;
    for (const auto &result : g_scan_results) {
        if (!is_driver_category(result.target.category)) {
            continue;
        }
        for (const auto &package : result.driver_packages) {
            if (!is_cleanable(package)) {
                continue;
            }
            found = true;
            const auto selected = selection.driver_packages.find(
                DriverPackageSelectionKey{package.id.category, package.id.task_id});
            if (selected == selection.driver_packages.end() || !selected->second) {
                return false;
            }
        }
    }
    return found;
}

bool all_optional_categories_selected(const CleanupSelectionState &selection) {
    bool found = false;
    for (const auto &result : g_scan_results) {
        if (is_driver_category(result.target.category) || result.target.selected_by_default ||
            !is_cleanable(result)) {
            continue;
        }
        found = true;
        const auto selected = selection.categories.find(result.target.category);
        if (selected == selection.categories.end() || !selected->second) {
            return false;
        }
    }
    return found;
}

void select_all_driver_packages(bool selected) {
    for (const auto &result : g_scan_results) {
        for (const auto &package : result.driver_packages) {
            if (is_cleanable(package)) {
                g_scan_previous_selection.driver_packages[DriverPackageSelectionKey{
                    package.id.category, package.id.task_id}] = selected;
            }
        }
    }
}

void select_all_optional_categories(bool selected) {
    for (const auto &result : g_scan_results) {
        if (!is_driver_category(result.target.category) && !result.target.selected_by_default &&
            is_cleanable(result)) {
            g_scan_previous_selection.categories[result.target.category] = selected;
        }
    }
}

void update_selection_summary() {
    g_scan_previous_selection = current_selection();
    std::uintmax_t available_bytes = 0;
    std::uintmax_t selected_bytes = 0;
    bool has_selection = false;
    bool has_recommended_item = false;
    for (const auto &result : g_scan_results) {
        if (is_driver_category(result.target.category)) {
            for (const auto &package : result.driver_packages) {
                if (!is_cleanable(package)) {
                    continue;
                }
                available_bytes += package.size_bytes;
                has_recommended_item = true;
                const auto selected = g_scan_previous_selection.driver_packages.find(
                    DriverPackageSelectionKey{package.id.category, package.id.task_id});
                if (selected != g_scan_previous_selection.driver_packages.end() &&
                    selected->second) {
                    selected_bytes += package.size_bytes;
                    has_selection = true;
                }
            }
            continue;
        }
        if (!is_cleanable(result)) {
            continue;
        }
        available_bytes += result.size_bytes;
        has_recommended_item = has_recommended_item || result.target.selected_by_default;
        const auto selected = g_scan_previous_selection.categories.find(result.target.category);
        if (selected != g_scan_previous_selection.categories.end() && selected->second) {
            selected_bytes += result.size_bytes;
            has_selection = true;
        }
    }

    const std::wstring summary = tr(nvidia_app_cleaner::TextId::summary_available_prefix) +
                                 format_size(available_bytes) + L"    " +
                                 tr(nvidia_app_cleaner::TextId::summary_selected_prefix) +
                                 format_size(selected_bytes);
    SetWindowTextW(g_summary_label, summary.c_str());

    std::wstring clean_label = tr(nvidia_app_cleaner::TextId::button_clean_selected);
    if (has_selection) {
        clean_label += L" · ";
        clean_label += format_size(selected_bytes);
    }
    SetWindowTextW(g_clean_button, clean_label.c_str());

    EnableWindow(g_clean_button, !g_scan_in_progress && has_selection);
    EnableWindow(g_clear_selection_button, !g_scan_in_progress && has_selection);
    EnableWindow(g_select_recommended_button, !g_scan_in_progress && has_recommended_item);
}

void update_scan_status() {
    if (g_scan_in_progress) {
        SetWindowTextW(g_status_label, tr(nvidia_app_cleaner::TextId::status_scanning));
        return;
    }

    std::wstring status = tr(nvidia_app_cleaner::TextId::status_scan_complete);
    if (g_repairable_count > 0) {
        status += tr(nvidia_app_cleaner::TextId::status_repair_available);
    }
    SetWindowTextW(g_status_label, status.c_str());
}

void update_repair_page_status() {
    if (g_scan_in_progress) {
        SetWindowTextW(g_repair_status_label, tr(nvidia_app_cleaner::TextId::status_scanning));
        EnableWindow(g_repair_button, FALSE);
        return;
    }
    if (g_repairable_count == 0) {
        SetWindowTextW(g_repair_status_label,
                       tr(nvidia_app_cleaner::TextId::repair_candidates_none));
        EnableWindow(g_repair_button, FALSE);
        return;
    }

    const std::wstring status = tr(nvidia_app_cleaner::TextId::repair_candidates_count_prefix) +
                                std::to_wstring(g_repairable_count);
    SetWindowTextW(g_repair_status_label, status.c_str());
    EnableWindow(g_repair_button, TRUE);
}

void render_scan_results(const CleanupSelectionState &selection) {
    g_refreshing_results = true;
    g_scan_previous_selection = selection;
    ListView_DeleteAllItems(g_list_view);
    g_displayed_cleanup_items.clear();

    for (std::size_t result_index = 0; result_index < g_scan_results.size(); ++result_index) {
        const auto &scan_result = g_scan_results[result_index];
        if (is_driver_category(scan_result.target.category)) {
            for (const auto &package : scan_result.driver_packages) {
                g_scan_previous_selection.driver_packages.try_emplace(
                    DriverPackageSelectionKey{package.id.category, package.id.task_id},
                    is_cleanable(package));
            }
            continue;
        }
        g_scan_previous_selection.categories.try_emplace(
            scan_result.target.category,
            is_cleanable(scan_result) && scan_result.target.selected_by_default);
    }

    const auto insert_row = [&](CleanupItemKind kind, std::size_t result_index,
                                std::optional<std::size_t> package_index, const std::wstring &name,
                                const std::wstring &location, std::uintmax_t size,
                                const wchar_t *status, bool selected) {
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = ListView_GetItemCount(g_list_view);
        item.pszText = const_cast<wchar_t *>(name.c_str());
        const int row = ListView_InsertItem(g_list_view, &item);
        if (row < 0) {
            return;
        }
        g_displayed_cleanup_items.push_back({kind, result_index, package_index});
        set_subitem(row, 1, location);
        set_subitem(row, 2, format_size(size));
        set_subitem(row, 3, status);
        ListView_SetCheckState(g_list_view, row, selected ? TRUE : FALSE);
    };

    for (std::size_t result_index = 0; result_index < g_scan_results.size(); ++result_index) {
        const auto &result = g_scan_results[result_index];
        if (is_driver_category(result.target.category) || !result.target.selected_by_default) {
            continue;
        }
        const auto selected = g_scan_previous_selection.categories.find(result.target.category);
        insert_row(CleanupItemKind::category, result_index, std::nullopt,
                   category_text(result.target.category), result.target.path.wstring(),
                   result.size_bytes, state_text(result.state),
                   is_cleanable(result) && selected != g_scan_previous_selection.categories.end() &&
                       selected->second);
    }

    std::uintmax_t rollback_size = 0;
    bool has_rollback_package = false;
    for (const auto &result : g_scan_results) {
        if (!is_driver_category(result.target.category)) {
            continue;
        }
        for (const auto &package : result.driver_packages) {
            if (is_cleanable(package)) {
                has_rollback_package = true;
                rollback_size += package.size_bytes;
            }
        }
    }
    const std::wstring rollback_name =
        std::wstring(g_rollback_drivers_expanded ? L"▼  " : L"▶  ") +
        tr(nvidia_app_cleaner::TextId::cleanup_group_rollback_drivers);
    insert_row(CleanupItemKind::rollback_parent, 0, std::nullopt, rollback_name, L"", rollback_size,
               tr(has_rollback_package ? nvidia_app_cleaner::TextId::state_found
                                       : nvidia_app_cleaner::TextId::state_empty),
               all_driver_packages_selected(g_scan_previous_selection));

    if (g_rollback_drivers_expanded) {
        for (std::size_t result_index = 0; result_index < g_scan_results.size(); ++result_index) {
            const auto &result = g_scan_results[result_index];
            if (!is_driver_category(result.target.category)) {
                continue;
            }
            for (std::size_t package_index = 0; package_index < result.driver_packages.size();
                 ++package_index) {
                const auto &package = result.driver_packages[package_index];
                const auto selected = g_scan_previous_selection.driver_packages.find(
                    DriverPackageSelectionKey{package.id.category, package.id.task_id});
                insert_row(CleanupItemKind::driver_package, result_index, package_index,
                           L"    " + driver_package_name(package),
                           driver_package_display_path(package).wstring(), package.size_bytes,
                           tr(nvidia_app_cleaner::TextId::state_found),
                           is_cleanable(package) &&
                               selected != g_scan_previous_selection.driver_packages.end() &&
                               selected->second);
            }
        }
    }

    std::uintmax_t optional_size = 0;
    bool has_optional_item = false;
    for (const auto &result : g_scan_results) {
        if (!is_driver_category(result.target.category) && !result.target.selected_by_default &&
            is_cleanable(result)) {
            has_optional_item = true;
            optional_size += result.size_bytes;
        }
    }
    const std::wstring optional_name = std::wstring(g_optional_cleanup_expanded ? L"▼  " : L"▶  ") +
                                       tr(nvidia_app_cleaner::TextId::cleanup_group_optional);
    insert_row(CleanupItemKind::optional_parent, 0, std::nullopt, optional_name, L"", optional_size,
               tr(has_optional_item ? nvidia_app_cleaner::TextId::state_found
                                    : nvidia_app_cleaner::TextId::state_empty),
               all_optional_categories_selected(g_scan_previous_selection));

    if (g_optional_cleanup_expanded) {
        for (std::size_t result_index = 0; result_index < g_scan_results.size(); ++result_index) {
            const auto &result = g_scan_results[result_index];
            if (is_driver_category(result.target.category) || result.target.selected_by_default) {
                continue;
            }
            const auto selected = g_scan_previous_selection.categories.find(result.target.category);
            insert_row(CleanupItemKind::category, result_index, std::nullopt,
                       L"    " + std::wstring(category_text(result.target.category)),
                       result.target.path.wstring(), result.size_bytes, state_text(result.state),
                       is_cleanable(result) &&
                           selected != g_scan_previous_selection.categories.end() &&
                           selected->second);
        }
    }

    g_refreshing_results = false;
    update_selection_summary();
}

void set_scanning_state() {
    g_scan_in_progress = true;
    EnableWindow(g_list_view, FALSE);
    EnableWindow(g_scan_button, FALSE);
    EnableWindow(g_repair_button, FALSE);
    EnableWindow(g_select_recommended_button, FALSE);
    EnableWindow(g_clear_selection_button, FALSE);
    EnableWindow(g_clean_button, FALSE);
    update_installed_driver_text();
    update_repair_page_status();
    update_scan_status();
}

void restore_scan_controls() {
    EnableWindow(g_list_view, TRUE);
    EnableWindow(g_scan_button, TRUE);
    update_selection_summary();
    update_installed_driver_text();
    update_repair_page_status();
    update_scan_status();
}

void begin_scan(HWND owner) {
    if (g_scan_in_progress) {
        return;
    }

    const auto program_data = nvidia_app_cleaner::windows::program_data_path();
    if (program_data.empty()) {
        MessageBoxW(owner, tr(nvidia_app_cleaner::TextId::program_data_missing),
                    tr(nvidia_app_cleaner::TextId::window_title), MB_OK | MB_ICONERROR);
        return;
    }
    const auto program_files = nvidia_app_cleaner::windows::program_files_path();
    if (program_files.empty()) {
        MessageBoxW(owner, tr(nvidia_app_cleaner::TextId::program_files_missing),
                    tr(nvidia_app_cleaner::TextId::window_title), MB_OK | MB_ICONERROR);
        return;
    }

    if (g_scan_thread.joinable()) {
        g_scan_thread.join();
    }
    g_scan_previous_selection = current_selection();
    const auto targets = nvidia_app_cleaner::default_scan_targets(program_data, program_files);
    const auto framework_root = nvidia_app_cleaner::windows::update_framework_root();
    set_scanning_state();

    try {
        g_scan_thread = std::jthread([owner, targets, framework_root](std::stop_token stop_token) {
            ScanCompletion completion;
            try {
                completion.installed_drivers =
                    nvidia_app_cleaner::windows::installed_nvidia_drivers();
                const nvidia_app_cleaner::CacheScanner scanner;
                completion.results = scanner.scan_all(targets, stop_token);
                if (stop_token.stop_requested()) {
                    return;
                }

                const nvidia_app_cleaner::DriverDownloadRepair repair;
                for (const auto channel : {nvidia_app_cleaner::DriverChannel::game_ready,
                                           nvidia_app_cleaner::DriverChannel::studio}) {
                    if (repair.assess(framework_root, channel).state ==
                        nvidia_app_cleaner::DriverDownloadState::broken_file_location) {
                        ++completion.repairable_count;
                    }
                }
            } catch (...) {
                completion.failed = true;
            }

            if (stop_token.stop_requested()) {
                return;
            }
            {
                const std::scoped_lock lock(g_scan_mutex);
                g_pending_scan = std::move(completion);
            }
            PostMessageW(owner, kScanCompletedMessage, 0, 0);
        });
    } catch (const std::exception &error) {
        g_scan_in_progress = false;
        restore_scan_controls();
        const std::wstring message = utf8_to_wide(error.what());
        MessageBoxW(owner, message.c_str(), tr(nvidia_app_cleaner::TextId::window_title),
                    MB_OK | MB_ICONERROR);
    }
}

void finish_scan(HWND owner) {
    std::optional<ScanCompletion> completion;
    {
        const std::scoped_lock lock(g_scan_mutex);
        completion = std::move(g_pending_scan);
        g_pending_scan.reset();
    }
    if (!completion) {
        return;
    }
    if (g_scan_thread.joinable()) {
        g_scan_thread.join();
    }

    g_scan_in_progress = false;
    if (completion->failed) {
        restore_scan_controls();
        MessageBoxW(owner, tr(nvidia_app_cleaner::TextId::unknown_error),
                    tr(nvidia_app_cleaner::TextId::window_title), MB_OK | MB_ICONERROR);
        return;
    }

    g_scan_results = std::move(completion->results);
    g_installed_drivers = std::move(completion->installed_drivers);
    g_repairable_count = completion->repairable_count;
    render_scan_results(g_scan_previous_selection);
    update_installed_driver_text();
    restore_scan_controls();
}

void select_recommended_items() {
    g_scan_previous_selection = current_selection();
    for (const auto &result : g_scan_results) {
        if (is_driver_category(result.target.category)) {
            for (const auto &package : result.driver_packages) {
                g_scan_previous_selection.driver_packages[DriverPackageSelectionKey{
                    package.id.category, package.id.task_id}] = is_cleanable(package);
            }
            continue;
        }
        g_scan_previous_selection.categories[result.target.category] =
            is_cleanable(result) && result.target.selected_by_default;
    }
    render_scan_results(g_scan_previous_selection);
}

void clear_selected_items() {
    g_scan_previous_selection = current_selection();
    for (auto &[category, selected] : g_scan_previous_selection.categories) {
        static_cast<void>(category);
        selected = false;
    }
    for (auto &[package, selected] : g_scan_previous_selection.driver_packages) {
        static_cast<void>(package);
        selected = false;
    }
    render_scan_results(g_scan_previous_selection);
}

int run_elevated_repair() {
    if (nvidia_app_cleaner::windows::nvidia_app_is_running()) {
        MessageBoxW(nullptr, tr(nvidia_app_cleaner::TextId::app_still_running),
                    tr(nvidia_app_cleaner::TextId::window_title), MB_OK | MB_ICONWARNING);
        return 1;
    }

    nvidia_app_cleaner::windows::NvidiaUpdateServicePause service_pause;
    const auto pause_result = service_pause.pause();
    if (!pause_result) {
        const std::wstring message = tr(nvidia_app_cleaner::TextId::service_pause_failed) +
                                     service_error_message(pause_result);
        MessageBoxW(nullptr, message.c_str(), tr(nvidia_app_cleaner::TextId::window_title),
                    MB_OK | MB_ICONERROR);
        return 1;
    }

    const auto framework_root = nvidia_app_cleaner::windows::update_framework_root();
    if (framework_root.empty()) {
        static_cast<void>(service_pause.resume());
        MessageBoxW(nullptr, tr(nvidia_app_cleaner::TextId::framework_missing),
                    tr(nvidia_app_cleaner::TextId::window_title), MB_OK | MB_ICONERROR);
        return 1;
    }
    const nvidia_app_cleaner::DriverDownloadRepair repair;
    std::wstring repaired;
    std::wstring failures;
    for (const auto channel : {nvidia_app_cleaner::DriverChannel::game_ready,
                               nvidia_app_cleaner::DriverChannel::studio}) {
        const auto assessment = repair.assess(framework_root, channel);
        if (assessment.state != nvidia_app_cleaner::DriverDownloadState::broken_file_location) {
            continue;
        }

        const auto backup = nvidia_app_cleaner::windows::create_backup_path(channel);
        if (backup.empty()) {
            failures += std::wstring(channel_name(channel)) + L": " +
                        tr(nvidia_app_cleaner::TextId::local_app_data_missing) + L"\n";
            continue;
        }
        const auto result = repair.repair(framework_root, channel, backup);
        if (result.repaired) {
            repaired += std::wstring(L"• ") + channel_name(channel) + L"\n  " +
                        tr(nvidia_app_cleaner::TextId::backup_label) +
                        result.backup_directory.wstring() + L"\n";
        } else {
            failures +=
                std::wstring(channel_name(channel)) + L": " + utf8_to_wide(result.detail) + L"\n";
        }
    }

    const auto resume_result = service_pause.resume();
    if (!resume_result) {
        failures += tr(nvidia_app_cleaner::TextId::service_restart_label) +
                    service_error_message(resume_result) + L"\n";
    }

    if (repaired.empty() && failures.empty()) {
        MessageBoxW(nullptr, tr(nvidia_app_cleaner::TextId::stale_state_disappeared),
                    tr(nvidia_app_cleaner::TextId::window_title), MB_OK | MB_ICONINFORMATION);
        return 0;
    }
    if (!failures.empty()) {
        const std::wstring message =
            tr(nvidia_app_cleaner::TextId::repair_not_completed) + failures +
            tr(nvidia_app_cleaner::TextId::successful_repairs_label) + repaired;
        MessageBoxW(nullptr, message.c_str(), tr(nvidia_app_cleaner::TextId::window_title),
                    MB_OK | MB_ICONERROR);
        return 1;
    }

    const std::wstring message = tr(nvidia_app_cleaner::TextId::repair_succeeded) + repaired;
    MessageBoxW(nullptr, message.c_str(), tr(nvidia_app_cleaner::TextId::window_title),
                MB_OK | MB_ICONINFORMATION);
    return 0;
}

bool launch_elevated_repair(HWND owner) {
    const std::wstring parameters = g_language == nvidia_app_cleaner::UiLanguage::simplified_chinese
                                        ? L"--repair-stuck-downloads --language=zh-CN"
                                        : L"--repair-stuck-downloads --language=en";
    std::uint32_t error = ERROR_SUCCESS;
    const auto result = nvidia_app_cleaner::windows::launch_current_executable_elevated(
        owner, parameters, true, error);
    if (result == nvidia_app_cleaner::windows::ElevatedLaunchResult::cancelled) {
        return false;
    }
    if (result == nvidia_app_cleaner::windows::ElevatedLaunchResult::failed) {
        const nvidia_app_cleaner::TextId error_text =
            error == ERROR_FILE_NOT_FOUND || error == ERROR_INSUFFICIENT_BUFFER
                ? nvidia_app_cleaner::TextId::executable_missing
                : nvidia_app_cleaner::TextId::repair_process_failed;
        std::wstring message = tr(error_text);
        if (error_text == nvidia_app_cleaner::TextId::repair_process_failed) {
            message += windows_error_message(error);
        }
        MessageBoxW(owner, message.c_str(), tr(nvidia_app_cleaner::TextId::window_title),
                    MB_OK | MB_ICONERROR);
        return false;
    }
    return true;
}

void repair_stuck_downloads(HWND owner) {
    if (nvidia_app_cleaner::windows::nvidia_app_is_running()) {
        MessageBoxW(owner, tr(nvidia_app_cleaner::TextId::exit_app_first),
                    tr(nvidia_app_cleaner::TextId::window_title), MB_OK | MB_ICONWARNING);
        return;
    }

    const auto framework_root = nvidia_app_cleaner::windows::update_framework_root();
    if (framework_root.empty()) {
        MessageBoxW(owner, tr(nvidia_app_cleaner::TextId::framework_missing),
                    tr(nvidia_app_cleaner::TextId::window_title), MB_OK | MB_ICONERROR);
        return;
    }

    const nvidia_app_cleaner::DriverDownloadRepair repair;
    std::vector<nvidia_app_cleaner::DriverChannel> repairable_channels;
    std::wstring description = tr(nvidia_app_cleaner::TextId::repair_candidates_intro);
    for (const auto channel : {nvidia_app_cleaner::DriverChannel::game_ready,
                               nvidia_app_cleaner::DriverChannel::studio}) {
        const auto assessment = repair.assess(framework_root, channel);
        if (assessment.state != nvidia_app_cleaner::DriverDownloadState::broken_file_location) {
            continue;
        }
        repairable_channels.push_back(channel);
        description += L"• ";
        description += channel_name(channel);
        if (!assessment.version.empty()) {
            description += L" ";
            description += utf8_to_wide(assessment.version);
        }
        description += L"\n";
    }

    if (repairable_channels.empty()) {
        MessageBoxW(owner, tr(nvidia_app_cleaner::TextId::no_stale_record),
                    tr(nvidia_app_cleaner::TextId::window_title), MB_OK | MB_ICONINFORMATION);
        begin_scan(owner);
        return;
    }

    description += tr(nvidia_app_cleaner::TextId::repair_confirmation);
    if (MessageBoxW(owner, description.c_str(), tr(nvidia_app_cleaner::TextId::repair_dialog_title),
                    MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) {
        return;
    }
    launch_elevated_repair(owner);
    begin_scan(owner);
}

bool category_uses_update_service(nvidia_app_cleaner::CacheCategory category) {
    return category != nvidia_app_cleaner::CacheCategory::installer2 &&
           category != nvidia_app_cleaner::CacheCategory::ngx_models;
}

bool is_driver_category(nvidia_app_cleaner::CacheCategory category) {
    return nvidia_app_cleaner::is_driver_package_category(category);
}

bool category_has_download_status(nvidia_app_cleaner::CacheCategory category) {
    return is_driver_category(category) ||
           category == nvidia_app_cleaner::CacheCategory::nvidia_app_update;
}

nvidia_app_cleaner::DriverChannel download_channel(nvidia_app_cleaner::CacheCategory category) {
    switch (category) {
    case nvidia_app_cleaner::CacheCategory::game_ready_driver:
        return nvidia_app_cleaner::DriverChannel::game_ready;
    case nvidia_app_cleaner::CacheCategory::studio_driver:
        return nvidia_app_cleaner::DriverChannel::studio;
    case nvidia_app_cleaner::CacheCategory::nvidia_app_update:
        return nvidia_app_cleaner::DriverChannel::nvidia_app;
    default:
        return nvidia_app_cleaner::DriverChannel::game_ready;
    }
}

bool discard_download_status_records(const std::filesystem::path &framework_root,
                                     nvidia_app_cleaner::DriverChannel channel,
                                     std::wstring &failure) {
    const nvidia_app_cleaner::DriverDownloadRepair repair;
    for (std::size_t record_index = 0; record_index < 32; ++record_index) {
        const auto assessment = repair.assess(framework_root, channel);
        if (assessment.state == nvidia_app_cleaner::DriverDownloadState::no_record) {
            return true;
        }
        if (assessment.state != nvidia_app_cleaner::DriverDownloadState::ready_to_install &&
            assessment.state != nvidia_app_cleaner::DriverDownloadState::broken_file_location) {
            failure = utf8_to_wide(assessment.detail.empty()
                                       ? "An active or unsupported download record was not removed"
                                       : assessment.detail);
            return false;
        }

        const auto result = repair.discard_completed_download(framework_root, channel);
        if (!result.repaired) {
            failure = utf8_to_wide(result.detail);
            return false;
        }
    }
    failure = L"Too many completed-download status records were found.";
    return false;
}

int run_elevated_cleanup(std::span<const nvidia_app_cleaner::CacheCategory> categories,
                         std::span<const nvidia_app_cleaner::DriverPackageId> driver_packages) {
    if (nvidia_app_cleaner::windows::nvidia_app_is_running()) {
        MessageBoxW(nullptr, tr(nvidia_app_cleaner::TextId::cleanup_exit_app_first),
                    tr(nvidia_app_cleaner::TextId::cleanup_dialog_title), MB_OK | MB_ICONWARNING);
        return 1;
    }

    const auto program_data = nvidia_app_cleaner::windows::program_data_path();
    const auto program_files = nvidia_app_cleaner::windows::program_files_path();
    if (program_data.empty() || program_files.empty()) {
        const auto message_id = program_data.empty()
                                    ? nvidia_app_cleaner::TextId::program_data_missing
                                    : nvidia_app_cleaner::TextId::program_files_missing;
        MessageBoxW(nullptr, tr(message_id), tr(nvidia_app_cleaner::TextId::cleanup_dialog_title),
                    MB_OK | MB_ICONERROR);
        return 1;
    }

    const auto approved_targets =
        nvidia_app_cleaner::default_scan_targets(program_data, program_files);
    const nvidia_app_cleaner::CacheCleaner cleaner(approved_targets);
    const nvidia_app_cleaner::CacheScanner scanner;
    const bool needs_service =
        !driver_packages.empty() ||
        std::any_of(categories.begin(), categories.end(), category_uses_update_service);
    nvidia_app_cleaner::windows::NvidiaUpdateServicePause service_pause;
    bool service_available = true;
    std::wstring failures;
    if (needs_service) {
        const auto pause_result = service_pause.pause();
        if (!pause_result) {
            service_available = false;
            failures = tr(nvidia_app_cleaner::TextId::cleanup_service_pause_failed) +
                       service_error_message(pause_result) + L"\n";
        }
    }

    const auto framework_root = nvidia_app_cleaner::windows::update_framework_root();
    std::wstring cleaned;
    std::uintmax_t total_removed = 0;
    std::size_t completed_categories = 0;
    for (const auto category : categories) {
        if (category_uses_update_service(category) && !service_available) {
            continue;
        }

        const auto approved_target =
            std::find_if(approved_targets.begin(), approved_targets.end(),
                         [category](const auto &target) { return target.category == category; });
        if (approved_target == approved_targets.end()) {
            failures += std::wstring(L"• ") + category_text(category) + L": " +
                        tr(nvidia_app_cleaner::TextId::state_unsafe_path) + L"\n";
            continue;
        }
        const auto preflight = scanner.scan(*approved_target);
        if (preflight.state == nvidia_app_cleaner::ScanState::unsafe_path ||
            preflight.state == nvidia_app_cleaner::ScanState::partial ||
            preflight.state == nvidia_app_cleaner::ScanState::inaccessible) {
            failures += std::wstring(L"• ") + category_text(category) + L": " +
                        utf8_to_wide(preflight.detail) + L"\n";
            continue;
        }

        if (category_has_download_status(category)) {
            std::wstring status_failure;
            if (!discard_download_status_records(framework_root, download_channel(category),
                                                 status_failure)) {
                failures +=
                    std::wstring(L"• ") + category_text(category) + L": " + status_failure + L"\n";
                continue;
            }
        }

        const auto result = cleaner.clean(category);
        using nvidia_app_cleaner::CleanupState;
        if (result.state == CleanupState::cleaned || result.state == CleanupState::nothing_to_do ||
            result.state == CleanupState::not_found) {
            ++completed_categories;
            total_removed += result.bytes_removed;
            if (result.state == CleanupState::cleaned) {
                cleaned += std::wstring(L"• ") + category_text(category) + L": " +
                           format_size(result.bytes_removed) + L"\n";
            }
            continue;
        }

        failures += std::wstring(L"• ") + category_text(category) + L": " +
                    utf8_to_wide(result.detail) + L"\n";
        if (result.state == CleanupState::partial) {
            total_removed += result.bytes_removed;
        }
    }

    for (const auto &package_id : driver_packages) {
        if (!service_available) {
            continue;
        }

        const auto approved_target =
            std::find_if(approved_targets.begin(), approved_targets.end(), [&](const auto &target) {
                return target.category == package_id.category;
            });
        const auto channel = download_channel(package_id.category);
        std::wstring package_label =
            channel_name(channel) + std::wstring(L" ") + package_id.task_id;
        if (approved_target == approved_targets.end()) {
            failures += L"• " + package_label + L": " +
                        tr(nvidia_app_cleaner::TextId::state_unsafe_path) + L"\n";
            continue;
        }

        const auto preflight = scanner.scan(*approved_target);
        if (preflight.state == nvidia_app_cleaner::ScanState::unsafe_path ||
            preflight.state == nvidia_app_cleaner::ScanState::partial ||
            preflight.state == nvidia_app_cleaner::ScanState::inaccessible) {
            failures += L"• " + package_label + L": " + utf8_to_wide(preflight.detail) + L"\n";
            continue;
        }
        const auto scanned_package =
            std::find_if(preflight.driver_packages.begin(), preflight.driver_packages.end(),
                         [&](const auto &candidate) { return candidate.id == package_id; });
        if (scanned_package == preflight.driver_packages.end()) {
            failures += L"• " + package_label + L": " +
                        tr(nvidia_app_cleaner::TextId::state_not_found) + L"\n";
            continue;
        }
        package_label = driver_package_name(*scanned_package);

        std::string task_id;
        task_id.reserve(package_id.task_id.size());
        for (const wchar_t character : package_id.task_id) {
            task_id.push_back(static_cast<char>(character));
        }
        const nvidia_app_cleaner::DriverDownloadRepair repair;
        const auto assessment = repair.assess_task(framework_root, channel, task_id);
        if (assessment.state == nvidia_app_cleaner::DriverDownloadState::downloading ||
            assessment.state == nvidia_app_cleaner::DriverDownloadState::unsupported_record ||
            assessment.state == nvidia_app_cleaner::DriverDownloadState::inaccessible) {
            failures += L"• " + package_label + L": " +
                        utf8_to_wide(assessment.detail.empty()
                                         ? "The selected driver package is still active"
                                         : assessment.detail) +
                        L"\n";
            continue;
        }
        if (assessment.state == nvidia_app_cleaner::DriverDownloadState::ready_to_install ||
            assessment.state == nvidia_app_cleaner::DriverDownloadState::broken_file_location) {
            const auto discard =
                repair.discard_completed_download_task(framework_root, channel, task_id);
            if (!discard.repaired) {
                failures += L"• " + package_label + L": " + utf8_to_wide(discard.detail) + L"\n";
                continue;
            }
        }

        const auto result = cleaner.clean_driver_package(package_id);
        using nvidia_app_cleaner::CleanupState;
        if (result.state == CleanupState::cleaned || result.state == CleanupState::nothing_to_do ||
            result.state == CleanupState::not_found) {
            ++completed_categories;
            total_removed += result.bytes_removed;
            if (result.state == CleanupState::cleaned) {
                cleaned +=
                    L"• " + package_label + L": " + format_size(result.bytes_removed) + L"\n";
            }
            continue;
        }

        failures += L"• " + package_label + L": " + utf8_to_wide(result.detail) + L"\n";
        if (result.state == CleanupState::partial) {
            total_removed += result.bytes_removed;
        }
    }

    if (needs_service && service_available) {
        const auto resume_result = service_pause.resume();
        if (!resume_result) {
            failures += tr(nvidia_app_cleaner::TextId::service_restart_label) +
                        service_error_message(resume_result) + L"\n";
        }
    }

    std::wstring message;
    UINT icon = MB_ICONINFORMATION;
    if (failures.empty()) {
        message = tr(nvidia_app_cleaner::TextId::cleanup_succeeded);
    } else if (completed_categories > 0 || total_removed > 0) {
        message = tr(nvidia_app_cleaner::TextId::cleanup_partial);
        icon = MB_ICONWARNING;
    } else {
        message = tr(nvidia_app_cleaner::TextId::cleanup_failed);
        icon = MB_ICONERROR;
    }
    if (total_removed > 0) {
        message += tr(nvidia_app_cleaner::TextId::cleanup_removed_prefix) +
                   format_size(total_removed) +
                   tr(nvidia_app_cleaner::TextId::cleanup_removed_suffix);
    } else if (failures.empty()) {
        message += tr(nvidia_app_cleaner::TextId::cleanup_no_changes);
        message += L"\n";
    }
    message += cleaned;
    if (!failures.empty()) {
        message += L"\n";
        message += failures;
    }
    MessageBoxW(nullptr, message.c_str(), tr(nvidia_app_cleaner::TextId::cleanup_dialog_title),
                MB_OK | icon);
    return failures.empty() ? 0 : 1;
}

bool launch_elevated_cleanup(HWND owner, const SelectedCleanupItems &selected) {
    std::wstring parameters;
    if (!selected.categories.empty()) {
        parameters = nvidia_app_cleaner::build_cleanup_argument(selected.categories);
    }
    if (!selected.driver_packages.empty()) {
        if (!parameters.empty()) {
            parameters += L' ';
        }
        parameters +=
            nvidia_app_cleaner::build_driver_package_cleanup_argument(selected.driver_packages);
    }
    parameters += g_language == nvidia_app_cleaner::UiLanguage::simplified_chinese
                      ? L" --language=zh-CN"
                      : L" --language=en";
    std::uint32_t error = ERROR_SUCCESS;
    const auto result = nvidia_app_cleaner::windows::launch_current_executable_elevated(
        owner, parameters, true, error);
    if (result == nvidia_app_cleaner::windows::ElevatedLaunchResult::cancelled) {
        return false;
    }
    if (result == nvidia_app_cleaner::windows::ElevatedLaunchResult::failed) {
        const nvidia_app_cleaner::TextId error_text =
            error == ERROR_FILE_NOT_FOUND || error == ERROR_INSUFFICIENT_BUFFER
                ? nvidia_app_cleaner::TextId::executable_missing
                : nvidia_app_cleaner::TextId::cleanup_process_failed;
        std::wstring message = tr(error_text);
        if (error_text == nvidia_app_cleaner::TextId::cleanup_process_failed) {
            message += windows_error_message(error);
        }
        MessageBoxW(owner, message.c_str(), tr(nvidia_app_cleaner::TextId::cleanup_dialog_title),
                    MB_OK | MB_ICONERROR);
        return false;
    }
    return true;
}

void clean_selected(HWND owner) {
    const auto selected = selected_cleanup_items();
    if (selected.categories.empty() && selected.driver_packages.empty()) {
        MessageBoxW(owner, tr(nvidia_app_cleaner::TextId::cleanup_no_selection),
                    tr(nvidia_app_cleaner::TextId::cleanup_dialog_title),
                    MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (nvidia_app_cleaner::windows::nvidia_app_is_running()) {
        MessageBoxW(owner, tr(nvidia_app_cleaner::TextId::cleanup_exit_app_first),
                    tr(nvidia_app_cleaner::TextId::cleanup_dialog_title), MB_OK | MB_ICONWARNING);
        return;
    }

    std::wstring confirmation = tr(nvidia_app_cleaner::TextId::cleanup_confirmation_intro);
    bool includes_driver = false;
    bool includes_installer2 = false;
    bool includes_ngx = false;
    for (const auto category : selected.categories) {
        const auto result = std::find_if(
            g_scan_results.begin(), g_scan_results.end(),
            [category](const auto &candidate) { return candidate.target.category == category; });
        confirmation += std::wstring(L"• ") + category_text(category);
        if (result != g_scan_results.end()) {
            const std::wstring versions = joined_package_versions(*result);
            if (!versions.empty()) {
                confirmation += L" [" + versions + L"]";
            }
            confirmation += L": ";
            confirmation += format_size(result->size_bytes);
        }
        confirmation += L"\n";
        includes_driver = includes_driver || is_driver_category(category);
        includes_installer2 =
            includes_installer2 || category == nvidia_app_cleaner::CacheCategory::installer2;
        includes_ngx = includes_ngx || category == nvidia_app_cleaner::CacheCategory::ngx_models;
    }
    for (const auto &package_id : selected.driver_packages) {
        const auto *package = find_scanned_driver_package(package_id);
        confirmation += L"• ";
        if (package != nullptr) {
            confirmation +=
                driver_package_name(*package) + L": " + format_size(package->size_bytes);
        } else {
            confirmation += std::wstring(category_text(package_id.category)) + L" [" +
                            package_id.task_id + L"]";
        }
        confirmation += L"\n";
        includes_driver = true;
    }
    if (includes_driver) {
        confirmation += tr(nvidia_app_cleaner::TextId::cleanup_driver_warning);
    }
    if (includes_installer2) {
        confirmation += tr(nvidia_app_cleaner::TextId::cleanup_installer2_warning);
    }
    if (includes_ngx) {
        confirmation += tr(nvidia_app_cleaner::TextId::cleanup_ngx_warning);
    }
    confirmation += tr(nvidia_app_cleaner::TextId::cleanup_confirmation_suffix);
    if (MessageBoxW(owner, confirmation.c_str(),
                    tr(nvidia_app_cleaner::TextId::cleanup_dialog_title),
                    MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) {
        return;
    }

    static_cast<void>(launch_elevated_cleanup(owner, selected));
    begin_scan(owner);
}

void update_settings_status_text();

bool configure_owner_draw_menu(HMENU menu, bool top_level,
                               std::vector<std::unique_ptr<MenuItemVisual>> &menu_item_visuals) {
    const int item_count = GetMenuItemCount(menu);
    if (item_count < 0) {
        return false;
    }

    for (int position = 0; position < item_count; ++position) {
        wchar_t text[256]{};
        MENUITEMINFOW item{
            .cbSize = sizeof(MENUITEMINFOW),
            .fMask = MIIM_FTYPE | MIIM_STRING | MIIM_SUBMENU,
            .dwTypeData = text,
            .cch = static_cast<UINT>(std::size(text)),
        };
        if (GetMenuItemInfoW(menu, static_cast<UINT>(position), TRUE, &item) == FALSE) {
            return false;
        }

        if ((item.fType & MFT_SEPARATOR) == 0) {
            auto visual = std::make_unique<MenuItemVisual>(MenuItemVisual{
                .text = text,
                .top_level = top_level,
                .has_submenu = item.hSubMenu != nullptr,
            });
            MENUITEMINFOW owner_draw{
                .cbSize = sizeof(MENUITEMINFOW),
                .fMask = MIIM_FTYPE | MIIM_DATA,
                .fType = item.fType | MFT_OWNERDRAW,
                .dwItemData = reinterpret_cast<ULONG_PTR>(visual.get()),
            };
            if (SetMenuItemInfoW(menu, static_cast<UINT>(position), TRUE, &owner_draw) == FALSE) {
                return false;
            }
            menu_item_visuals.push_back(std::move(visual));
        }

        if (item.hSubMenu != nullptr &&
            !configure_owner_draw_menu(item.hSubMenu, false, menu_item_visuals)) {
            return false;
        }
    }
    return true;
}

int menu_scale(int value) {
    const UINT dpi =
        g_list_view != nullptr ? GetDpiForWindow(g_list_view) : USER_DEFAULT_SCREEN_DPI;
    return MulDiv(value, dpi != 0 ? static_cast<int>(dpi) : 96, 96);
}

bool measure_menu_item(MEASUREITEMSTRUCT &item) {
    if (item.CtlType != ODT_MENU || item.itemData == 0) {
        return false;
    }

    const auto *visual = reinterpret_cast<const MenuItemVisual *>(item.itemData);
    const HDC device_context = GetDC(nullptr);
    if (device_context == nullptr) {
        return false;
    }
    const HGDIOBJ previous_font = SelectObject(
        device_context, g_ui_font != nullptr
                            ? g_ui_font
                            : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)));
    RECT text_rect{};
    DrawTextW(device_context, visual->text.c_str(), static_cast<int>(visual->text.size()),
              &text_rect, DT_CALCRECT | DT_SINGLELINE);
    SelectObject(device_context, previous_font);
    ReleaseDC(nullptr, device_context);

    item.itemWidth = static_cast<UINT>(text_rect.right - text_rect.left +
                                       menu_scale(visual->top_level ? 20 : 48));
    item.itemHeight = static_cast<UINT>(menu_scale(visual->top_level ? 26 : 28));
    return true;
}

bool draw_menu_item(const DRAWITEMSTRUCT &item) {
    if (item.CtlType != ODT_MENU || item.itemData == 0) {
        return false;
    }

    const auto *visual = reinterpret_cast<const MenuItemVisual *>(item.itemData);
    const bool selected = (item.itemState & ODS_SELECTED) != 0;
    const bool disabled = (item.itemState & (ODS_DISABLED | ODS_GRAYED)) != 0;
    const bool checked = (item.itemState & ODS_CHECKED) != 0;
    const COLORREF background =
        selected ? (g_theme.dark_mode() ? RGB(62, 62, 66) : GetSysColor(COLOR_HIGHLIGHT))
                 : g_theme.background_color();
    const COLORREF foreground =
        disabled ? (g_theme.dark_mode() ? RGB(140, 140, 144) : GetSysColor(COLOR_GRAYTEXT))
                 : (selected && !g_theme.dark_mode() ? GetSysColor(COLOR_HIGHLIGHTTEXT)
                                                     : g_theme.text_color());

    SetDCBrushColor(item.hDC, background);
    FillRect(item.hDC, &item.rcItem, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    const HGDIOBJ previous_font = SelectObject(
        item.hDC, g_ui_font != nullptr ? g_ui_font
                                       : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)));
    const int previous_background_mode = SetBkMode(item.hDC, TRANSPARENT);
    const COLORREF previous_text_color = SetTextColor(item.hDC, foreground);

    RECT text_rect = item.rcItem;
    text_rect.left += menu_scale(visual->top_level ? 10 : 28);
    text_rect.right -= menu_scale(visual->has_submenu && !visual->top_level ? 22 : 8);
    DrawTextW(item.hDC, visual->text.c_str(), static_cast<int>(visual->text.size()), &text_rect,
              DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    if (checked && !visual->top_level) {
        RECT check_rect = item.rcItem;
        check_rect.left += menu_scale(6);
        check_rect.right = check_rect.left + menu_scale(16);
        DrawTextW(item.hDC, L"✓", 1, &check_rect, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    }
    if (visual->has_submenu && !visual->top_level) {
        RECT arrow_rect = item.rcItem;
        arrow_rect.left = arrow_rect.right - menu_scale(20);
        DrawTextW(item.hDC, L"›", 1, &arrow_rect, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    }

    SetTextColor(item.hDC, previous_text_color);
    SetBkMode(item.hDC, previous_background_mode);
    SelectObject(item.hDC, previous_font);
    return true;
}

bool replace_main_menu(HWND window) {
    static_cast<void>(tr(nvidia_app_cleaner::TextId::window_title));
    const HMENU new_menu = LoadMenuW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDR_MAIN_MENU));
    const HBRUSH new_background_brush = CreateSolidBrush(g_theme.background_color());
    std::vector<std::unique_ptr<MenuItemVisual>> new_menu_item_visuals;
    if (new_menu == nullptr || new_background_brush == nullptr ||
        !configure_owner_draw_menu(new_menu, true, new_menu_item_visuals)) {
        if (new_menu != nullptr) {
            DestroyMenu(new_menu);
        }
        if (new_background_brush != nullptr) {
            DeleteObject(new_background_brush);
        }
        return false;
    }

    MENUINFO menu_info{
        .cbSize = sizeof(MENUINFO),
        .fMask = MIM_BACKGROUND | MIM_APPLYTOSUBMENUS,
        .hbrBack = new_background_brush,
    };
    if (SetMenuInfo(new_menu, &menu_info) == FALSE || SetMenu(window, new_menu) == FALSE) {
        DestroyMenu(new_menu);
        DeleteObject(new_background_brush);
        return false;
    }

    const HMENU old_menu = g_main_menu;
    const HBRUSH old_background_brush = g_menu_background_brush;
    g_main_menu = new_menu;
    g_menu_background_brush = new_background_brush;
    g_menu_item_visuals = std::move(new_menu_item_visuals);
    if (old_menu != nullptr) {
        DestroyMenu(old_menu);
    }
    if (old_background_brush != nullptr) {
        DeleteObject(old_background_brush);
    }
    DrawMenuBar(window);
    return true;
}

void update_navigation_state() {
    if (g_main_menu == nullptr) {
        return;
    }

    CheckMenuItem(g_main_menu, IDM_VIEW_CLEANUP,
                  MF_BYCOMMAND | (g_current_page == Page::cleanup ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(g_main_menu, IDM_VIEW_REPAIR,
                  MF_BYCOMMAND | (g_current_page == Page::repair ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(g_main_menu, IDM_VIEW_SETTINGS,
                  MF_BYCOMMAND | (g_current_page == Page::settings ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuRadioItem(g_main_menu, IDM_LANGUAGE_ENGLISH, IDM_LANGUAGE_SIMPLIFIED_CHINESE,
                       g_language == nvidia_app_cleaner::UiLanguage::simplified_chinese
                           ? IDM_LANGUAGE_SIMPLIFIED_CHINESE
                           : IDM_LANGUAGE_ENGLISH,
                       MF_BYCOMMAND);
}

void hide_page_controls() {
    for (const HWND control :
         {g_heading_label, g_driver_info_label, g_summary_label, g_list_view, g_scan_button,
          g_clean_button, g_select_recommended_button, g_clear_selection_button, g_status_label,
          g_repair_title_label, g_repair_description_label, g_repair_status_label, g_repair_button,
          g_settings_title_label, g_settings_description_label, g_always_admin_checkbox,
          g_settings_status_label, g_settings_save_button}) {
        ShowWindow(control, SW_HIDE);
    }
}

void relayout(HWND window) {
    RECT client{};
    GetClientRect(window, &client);
    SendMessageW(window, WM_SIZE, SIZE_RESTORED,
                 MAKELPARAM(client.right - client.left, client.bottom - client.top));
}

void show_settings(HWND owner) {
    nvidia_app_cleaner::AppSettings settings;
    std::uint32_t error = ERROR_SUCCESS;
    const bool loaded = nvidia_app_cleaner::load_app_settings(settings, error);
    SendMessageW(g_always_admin_checkbox, BM_SETCHECK,
                 loaded && settings.always_run_as_administrator ? BST_CHECKED : BST_UNCHECKED, 0);
    EnableWindow(g_always_admin_checkbox, loaded ? TRUE : FALSE);
    EnableWindow(g_settings_save_button, loaded ? TRUE : FALSE);
    g_settings_status = loaded ? SettingsStatus::none : SettingsStatus::read_failed;
    g_settings_error = loaded ? ERROR_SUCCESS : error;
    update_settings_status_text();

    g_current_page = Page::settings;
    hide_page_controls();
    for (const HWND control :
         {g_settings_title_label, g_settings_description_label, g_always_admin_checkbox,
          g_settings_status_label, g_settings_save_button}) {
        ShowWindow(control, SW_SHOW);
    }
    update_navigation_state();
    relayout(owner);
    SetFocus(loaded ? g_always_admin_checkbox : g_settings_save_button);
}

void show_main_page(HWND window) {
    g_current_page = Page::cleanup;
    hide_page_controls();
    for (const HWND control :
         {g_heading_label, g_driver_info_label, g_summary_label, g_list_view, g_scan_button,
          g_clean_button, g_select_recommended_button, g_clear_selection_button, g_status_label}) {
        ShowWindow(control, SW_SHOW);
    }
    update_navigation_state();
    relayout(window);
    SetFocus(g_scan_button);
}

void show_repair_page(HWND window) {
    g_current_page = Page::repair;
    hide_page_controls();
    for (const HWND control : {g_repair_title_label, g_repair_description_label,
                               g_repair_status_label, g_repair_button}) {
        ShowWindow(control, SW_SHOW);
    }
    update_repair_page_status();
    update_navigation_state();
    relayout(window);
    SetFocus(g_repairable_count > 0 ? g_repair_button : window);
}

void save_settings() {
    nvidia_app_cleaner::AppSettings settings{
        .always_run_as_administrator =
            SendMessageW(g_always_admin_checkbox, BM_GETCHECK, 0, 0) == BST_CHECKED,
    };
    std::uint32_t error = ERROR_SUCCESS;
    if (nvidia_app_cleaner::save_app_settings(settings, error)) {
        g_settings_status = SettingsStatus::saved;
        g_settings_error = ERROR_SUCCESS;
    } else {
        g_settings_status = SettingsStatus::write_failed;
        g_settings_error = error;
    }
}

void add_column(int index, int width, const wchar_t *title) {
    LVCOLUMNW column{};
    column.mask = LVCF_FMT | LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    column.fmt = LVCFMT_LEFT;
    column.cx = width;
    column.iSubItem = index;
    column.pszText = const_cast<wchar_t *>(title);
    ListView_InsertColumn(g_list_view, index, &column);
}

void set_column_width(int index, int width) {
    if (ListView_GetColumnWidth(g_list_view, index) != width) {
        ListView_SetColumnWidth(g_list_view, index, width);
    }
}

void set_column_title(int index, const wchar_t *title) {
    LVCOLUMNW column{};
    column.mask = LVCF_TEXT;
    column.pszText = const_cast<wchar_t *>(title);
    ListView_SetColumn(g_list_view, index, &column);
}

void update_settings_status_text() {
    std::wstring status;
    switch (g_settings_status) {
    case SettingsStatus::none:
        break;
    case SettingsStatus::saved:
        status = tr(nvidia_app_cleaner::TextId::settings_saved);
        break;
    case SettingsStatus::read_failed:
        status = tr(nvidia_app_cleaner::TextId::settings_read_failed) +
                 windows_error_message(g_settings_error);
        break;
    case SettingsStatus::write_failed:
        status = tr(nvidia_app_cleaner::TextId::settings_write_failed) +
                 windows_error_message(g_settings_error);
        break;
    }
    SetWindowTextW(g_settings_status_label, status.c_str());
}

void apply_localized_text(HWND window) {
    SetWindowTextW(window, tr(nvidia_app_cleaner::TextId::window_title));
    static_cast<void>(replace_main_menu(window));
    SetWindowTextW(g_heading_label, tr(nvidia_app_cleaner::TextId::main_heading));
    update_installed_driver_text();
    SetWindowTextW(g_scan_button, tr(nvidia_app_cleaner::TextId::button_scan_again));
    SetWindowTextW(g_repair_button, tr(nvidia_app_cleaner::TextId::button_repair_stuck));
    SetWindowTextW(g_select_recommended_button,
                   tr(nvidia_app_cleaner::TextId::button_select_recommended));
    SetWindowTextW(g_clear_selection_button,
                   tr(nvidia_app_cleaner::TextId::button_clear_selection));
    SetWindowTextW(g_settings_save_button, tr(nvidia_app_cleaner::TextId::button_save));
    SetWindowTextW(g_repair_title_label, tr(nvidia_app_cleaner::TextId::navigation_repair));
    SetWindowTextW(g_repair_description_label,
                   tr(nvidia_app_cleaner::TextId::repair_page_description));
    SetWindowTextW(g_settings_title_label, tr(nvidia_app_cleaner::TextId::settings_page_title));
    SetWindowTextW(g_settings_description_label,
                   tr(nvidia_app_cleaner::TextId::settings_description));
    SetWindowTextW(g_always_admin_checkbox,
                   tr(nvidia_app_cleaner::TextId::settings_always_run_as_administrator));
    update_settings_status_text();
    set_column_title(0, tr(nvidia_app_cleaner::TextId::column_category));
    set_column_title(1, tr(nvidia_app_cleaner::TextId::column_location));
    set_column_title(2, tr(nvidia_app_cleaner::TextId::column_size));
    set_column_title(3, tr(nvidia_app_cleaner::TextId::column_status));
    const auto selection = current_selection();
    render_scan_results(selection);
    update_repair_page_status();
    update_navigation_state();
    update_scan_status();
}

void delete_fonts() {
    const auto delete_font = [](HFONT &font) {
        if (font != nullptr) {
            DeleteObject(font);
            font = nullptr;
        }
    };
    delete_font(g_ui_font);
    delete_font(g_heading_font);
    delete_font(g_summary_font);
    delete_font(g_primary_font);
}

HFONT create_font(HWND window, int point_size, int weight) {
    const UINT dpi = GetDpiForWindow(window);
    return CreateFontW(-MulDiv(point_size, dpi != 0 ? static_cast<int>(dpi) : 96, 72), 0, 0, 0,
                       weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS,
                       L"Segoe UI");
}

void set_control_font(HWND control, HFONT font) {
    const HFONT applied =
        font != nullptr ? font : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(applied), TRUE);
}

void create_and_apply_fonts(HWND window) {
    delete_fonts();
    g_ui_font = create_font(window, 10, FW_NORMAL);
    g_heading_font = create_font(window, 18, FW_SEMIBOLD);
    g_summary_font = create_font(window, 11, FW_SEMIBOLD);
    g_primary_font = create_font(window, 10, FW_SEMIBOLD);

    for (const HWND control :
         {g_list_view, g_driver_info_label, g_scan_button, g_repair_button,
          g_select_recommended_button, g_clear_selection_button, g_status_label,
          g_repair_description_label, g_repair_status_label, g_settings_description_label,
          g_always_admin_checkbox, g_settings_status_label}) {
        set_control_font(control, g_ui_font);
    }
    set_control_font(g_heading_label, g_heading_font);
    set_control_font(g_repair_title_label, g_heading_font);
    set_control_font(g_settings_title_label, g_heading_font);
    set_control_font(g_summary_label, g_summary_font);
    set_control_font(g_clean_button, g_primary_font);
    set_control_font(g_settings_save_button, g_primary_font);
}

void apply_theme(HWND window) { g_theme.apply(window, g_list_view); }

bool is_app_button(HWND control) {
    return control == g_scan_button || control == g_clean_button || control == g_repair_button ||
           control == g_select_recommended_button || control == g_clear_selection_button ||
           control == g_always_admin_checkbox || control == g_settings_save_button;
}

int scale_for_window(HWND window, int value) {
    const UINT dpi = GetDpiForWindow(window);
    return MulDiv(value, dpi != 0 ? static_cast<int>(dpi) : 96, 96);
}

int list_view_width() {
    RECT client{};
    return g_list_view != nullptr && GetClientRect(g_list_view, &client) != FALSE
               ? std::max(0, static_cast<int>(client.right - client.left))
               : 0;
}

int minimum_column_width(HWND window, int column_index) {
    constexpr std::array<int, 4> minimum_widths{
        kMinimumCategoryColumnWidthDip,
        kMinimumLocationColumnWidthDip,
        kMinimumSizeColumnWidthDip,
        kMinimumStatusColumnWidthDip,
    };
    return column_index >= 0 && column_index < static_cast<int>(minimum_widths.size())
               ? scale_for_window(window, minimum_widths[static_cast<std::size_t>(column_index)])
               : 0;
}

int constrained_tracked_column_width(HWND window, int column_index, int proposed_width) {
    if (column_index != g_tracked_list_column || column_index < 0 || column_index >= 3) {
        return proposed_width;
    }

    const int minimum_left_width = minimum_column_width(window, column_index);
    const int minimum_right_width = minimum_column_width(window, column_index + 1);
    const int maximum_left_width =
        std::max(minimum_left_width, g_tracked_column_pair_width - minimum_right_width);
    return std::clamp(proposed_width, minimum_left_width, maximum_left_width);
}

void resize_tracked_column_pair(HWND window, int proposed_left_width) {
    if (g_tracked_list_column < 0 || g_tracked_list_column >= 3) {
        return;
    }

    const int left_width =
        constrained_tracked_column_width(window, g_tracked_list_column, proposed_left_width);
    const int right_width = g_tracked_column_pair_width - left_width;
    const int current_left_width = ListView_GetColumnWidth(g_list_view, g_tracked_list_column);
    g_resizing_list_columns = true;
    if (left_width < current_left_width) {
        set_column_width(g_tracked_list_column, left_width);
        set_column_width(g_tracked_list_column + 1, right_width);
    } else {
        set_column_width(g_tracked_list_column + 1, right_width);
        set_column_width(g_tracked_list_column, left_width);
    }
    g_resizing_list_columns = false;
    ShowScrollBar(g_list_view, SB_HORZ, FALSE);
}

void resize_columns_to_fill(HWND window);

void resize_list_columns(HWND window) {
    if (g_list_view == nullptr) {
        return;
    }

    const int available_width = list_view_width();
    if (available_width == 0) {
        return;
    }

    if (g_list_columns_initialized) {
        resize_columns_to_fill(window);
        return;
    }

    const int category_width = scale_for_window(window, 250);
    const int size_width = scale_for_window(window, 100);
    const int status_width = minimum_column_width(window, 3);
    const int location_width =
        std::max(minimum_column_width(window, 1),
                 available_width - category_width - size_width - status_width);

    g_resizing_list_columns = true;
    set_column_width(0, category_width);
    set_column_width(1, location_width);
    set_column_width(2, size_width);
    set_column_width(3, status_width);
    g_resizing_list_columns = false;
    g_list_columns_initialized = true;
    const UINT dpi = GetDpiForWindow(window);
    g_list_columns_dpi = dpi != 0 ? dpi : USER_DEFAULT_SCREEN_DPI;
    resize_columns_to_fill(window);
}

void resize_columns_to_fill(HWND window) {
    if (g_list_view == nullptr || g_resizing_list_columns) {
        return;
    }

    const int available_width = list_view_width();
    if (available_width == 0) {
        return;
    }

    std::array<int, 4> widths{};
    int total_width = 0;
    for (int index = 0; index < static_cast<int>(widths.size()); ++index) {
        widths[static_cast<std::size_t>(index)] = ListView_GetColumnWidth(g_list_view, index);
        total_width += widths[static_cast<std::size_t>(index)];
    }

    int excess_width = std::max(0, total_width - available_width);
    for (int index = static_cast<int>(widths.size()) - 1; index >= 0 && excess_width > 0; --index) {
        int &width = widths[static_cast<std::size_t>(index)];
        const int shrinkable_width = std::max(0, width - minimum_column_width(window, index));
        const int shrink_width = std::min(excess_width, shrinkable_width);
        width -= shrink_width;
        excess_width -= shrink_width;
    }

    // WM_GETMINMAXINFO normally prevents this fallback. Keeping it avoids a horizontal scrollbar
    // if Windows temporarily gives the control less space during a DPI transition.
    for (int index = static_cast<int>(widths.size()) - 1; index >= 0 && excess_width > 0; --index) {
        int &width = widths[static_cast<std::size_t>(index)];
        const int shrink_width = std::min(excess_width, width);
        width -= shrink_width;
        excess_width -= shrink_width;
    }

    total_width = 0;
    for (const int width : widths) {
        total_width += width;
    }
    widths.back() += std::max(0, available_width - total_width);

    g_resizing_list_columns = true;
    for (int index = 0; index < static_cast<int>(widths.size()); ++index) {
        const int width = widths[static_cast<std::size_t>(index)];
        if (width < ListView_GetColumnWidth(g_list_view, index)) {
            set_column_width(index, width);
        }
    }
    for (int index = 0; index < static_cast<int>(widths.size()); ++index) {
        const int width = widths[static_cast<std::size_t>(index)];
        if (width > ListView_GetColumnWidth(g_list_view, index)) {
            set_column_width(index, width);
        }
    }
    g_resizing_list_columns = false;
    ShowScrollBar(g_list_view, SB_HORZ, FALSE);
}

void scale_list_columns_for_dpi(UINT new_dpi) {
    if (!g_list_columns_initialized || new_dpi == 0 || new_dpi == g_list_columns_dpi) {
        return;
    }

    g_resizing_list_columns = true;
    for (int index = 0; index < 3; ++index) {
        const int current_width = ListView_GetColumnWidth(g_list_view, index);
        set_column_width(index, MulDiv(current_width, static_cast<int>(new_dpi),
                                       static_cast<int>(g_list_columns_dpi)));
    }
    g_resizing_list_columns = false;
    g_list_columns_dpi = new_dpi;
}

void layout_controls(HWND window) {
    RECT client{};
    if (GetClientRect(window, &client) == FALSE) {
        return;
    }

    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    const int margin = scale_for_window(window, 20);
    const int scan_button_width = scale_for_window(window, 110);
    const int clean_button_width = scale_for_window(window, 165);
    const int repair_button_width = scale_for_window(window, 170);
    const int button_gap = scale_for_window(window, 8);
    const int control_height = scale_for_window(window, 34);
    const int status_height = scale_for_window(window, 22);
    const int settings_action_width = scale_for_window(window, 100);
    const int controls_y = height - margin - control_height;

    const int content_left = scale_for_window(window, 24);
    const int content_width = std::max(0, width - (content_left * 2));
    if (g_current_page == Page::settings) {
        MoveWindow(g_settings_title_label, content_left, scale_for_window(window, 22),
                   content_width, scale_for_window(window, 34), TRUE);
        MoveWindow(g_settings_description_label, content_left, scale_for_window(window, 68),
                   content_width, scale_for_window(window, 56), TRUE);
        MoveWindow(g_always_admin_checkbox, content_left, scale_for_window(window, 136),
                   content_width, scale_for_window(window, 44), TRUE);
        MoveWindow(g_settings_status_label, content_left, scale_for_window(window, 192),
                   content_width, scale_for_window(window, 56), TRUE);
        MoveWindow(g_settings_save_button, width - margin - settings_action_width, controls_y,
                   settings_action_width, control_height, TRUE);
        return;
    }

    if (g_current_page == Page::repair) {
        MoveWindow(g_repair_title_label, content_left, scale_for_window(window, 22), content_width,
                   scale_for_window(window, 34), TRUE);
        MoveWindow(g_repair_description_label, content_left, scale_for_window(window, 68),
                   content_width, scale_for_window(window, 54), TRUE);
        MoveWindow(g_repair_status_label, content_left, scale_for_window(window, 138),
                   content_width, status_height, TRUE);
        MoveWindow(g_repair_button, content_left, scale_for_window(window, 174),
                   repair_button_width, control_height, TRUE);
        return;
    }

    const int select_recommended_width = scale_for_window(window, 150);
    const int clear_selection_width = scale_for_window(window, 115);
    const int selection_row_height = scale_for_window(window, 30);
    const int selection_y = scale_for_window(window, 46);
    const int clear_selection_x = width - margin - clear_selection_width;
    const int select_recommended_x = clear_selection_x - button_gap - select_recommended_width;
    MoveWindow(g_heading_label, margin, scale_for_window(window, 16), scale_for_window(window, 180),
               scale_for_window(window, 34), TRUE);
    MoveWindow(g_summary_label, margin + scale_for_window(window, 190),
               scale_for_window(window, 24),
               std::max(0, width - margin - (margin + scale_for_window(window, 190))),
               status_height, TRUE);
    MoveWindow(g_driver_info_label, margin, selection_y + scale_for_window(window, 4),
               std::max(0, select_recommended_x - margin - button_gap), status_height, TRUE);
    MoveWindow(g_select_recommended_button, select_recommended_x, selection_y,
               select_recommended_width, selection_row_height, TRUE);
    MoveWindow(g_clear_selection_button, clear_selection_x, selection_y, clear_selection_width,
               selection_row_height, TRUE);

    const int list_top = scale_for_window(window, 86);
    const int list_bottom = controls_y - scale_for_window(window, 12);
    MoveWindow(g_list_view, margin, list_top, std::max(0, width - (margin * 2)),
               std::max(0, list_bottom - list_top), TRUE);
    resize_list_columns(window);

    const int clean_button_x = width - margin - clean_button_width;
    const int scan_button_x = clean_button_x - button_gap - scan_button_width;

    MoveWindow(g_scan_button, scan_button_x, controls_y, scan_button_width, control_height, TRUE);
    MoveWindow(g_clean_button, clean_button_x, controls_y, clean_button_width, control_height,
               TRUE);
    MoveWindow(g_status_label, margin, controls_y + scale_for_window(window, 6),
               std::max(0, scan_button_x - margin - button_gap), status_height, TRUE);
}

LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM w_param, LPARAM l_param) {
    switch (message) {
    case WM_CREATE: {
        g_heading_label =
            CreateWindowExW(0, L"STATIC", tr(nvidia_app_cleaner::TextId::main_heading),
                            WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX, 0, 0, 0, 0, window,
                            nullptr, nullptr, nullptr);
        g_driver_info_label =
            CreateWindowExW(0, L"STATIC", tr(nvidia_app_cleaner::TextId::current_driver_scanning),
                            WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX, 0, 0, 0, 0, window,
                            nullptr, nullptr, nullptr);
        g_summary_label =
            CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX, 0, 0,
                            0, 0, window, nullptr, nullptr, nullptr);
        if (g_heading_label == nullptr || g_driver_info_label == nullptr ||
            g_summary_label == nullptr) {
            return -1;
        }

        g_list_view =
            CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                            WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | WS_TABSTOP, 0, 0,
                            0, 0, window, nullptr, nullptr, nullptr);
        if (g_list_view == nullptr) {
            return -1;
        }
        ListView_SetExtendedListViewStyle(g_list_view, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER |
                                                           LVS_EX_CHECKBOXES);
        add_column(0, 250, tr(nvidia_app_cleaner::TextId::column_category));
        add_column(1, 480, tr(nvidia_app_cleaner::TextId::column_location));
        add_column(2, 100, tr(nvidia_app_cleaner::TextId::column_size));
        add_column(3, 100, tr(nvidia_app_cleaner::TextId::column_status));
        const HWND list_header = ListView_GetHeader(g_list_view);
        if (!g_theme.subclass_list_header(list_header)) {
            return -1;
        }

        g_scan_button = CreateWindowExW(
            0, L"BUTTON", tr(nvidia_app_cleaner::TextId::button_scan_again),
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0, 0, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kScanButtonId)), nullptr, nullptr);
        if (g_scan_button == nullptr) {
            return -1;
        }

        g_clean_button = CreateWindowExW(
            0, L"BUTTON", tr(nvidia_app_cleaner::TextId::button_clean_selected),
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 0, 0, 0, 0, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCleanButtonId)), nullptr, nullptr);
        if (g_clean_button == nullptr) {
            return -1;
        }

        g_repair_button = CreateWindowExW(
            0, L"BUTTON", tr(nvidia_app_cleaner::TextId::button_repair_stuck),
            WS_CHILD | WS_TABSTOP | BS_DEFPUSHBUTTON, 0, 0, 0, 0, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kRepairButtonId)), nullptr, nullptr);
        if (g_repair_button == nullptr) {
            return -1;
        }

        g_select_recommended_button = CreateWindowExW(
            0, L"BUTTON", tr(nvidia_app_cleaner::TextId::button_select_recommended),
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0, 0, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSelectRecommendedButtonId)), nullptr,
            nullptr);
        g_clear_selection_button =
            CreateWindowExW(0, L"BUTTON", tr(nvidia_app_cleaner::TextId::button_clear_selection),
                            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0, 0, window,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kClearSelectionButtonId)),
                            nullptr, nullptr);
        if (g_select_recommended_button == nullptr || g_clear_selection_button == nullptr) {
            return -1;
        }

        g_status_label = CreateWindowExW(
            0, L"STATIC", tr(nvidia_app_cleaner::TextId::status_scanning),
            WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 0, 0, window, nullptr, nullptr, nullptr);
        if (g_status_label == nullptr) {
            return -1;
        }

        g_repair_title_label = CreateWindowExW(
            0, L"STATIC", tr(nvidia_app_cleaner::TextId::navigation_repair),
            WS_CHILD | SS_LEFT | SS_NOPREFIX, 0, 0, 0, 0, window, nullptr, nullptr, nullptr);
        g_repair_description_label = CreateWindowExW(
            0, L"STATIC", tr(nvidia_app_cleaner::TextId::repair_page_description),
            WS_CHILD | SS_LEFT | SS_NOPREFIX, 0, 0, 0, 0, window, nullptr, nullptr, nullptr);
        g_repair_status_label = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | SS_LEFT | SS_NOPREFIX,
                                                0, 0, 0, 0, window, nullptr, nullptr, nullptr);
        if (g_repair_title_label == nullptr || g_repair_description_label == nullptr ||
            g_repair_status_label == nullptr) {
            return -1;
        }

        g_settings_title_label =
            CreateWindowExW(0, L"STATIC", tr(nvidia_app_cleaner::TextId::settings_page_title),
                            WS_CHILD | SS_LEFT, 0, 0, 0, 0, window, nullptr, nullptr, nullptr);
        g_settings_description_label =
            CreateWindowExW(0, L"STATIC", tr(nvidia_app_cleaner::TextId::settings_description),
                            WS_CHILD | SS_LEFT, 0, 0, 0, 0, window, nullptr, nullptr, nullptr);
        g_always_admin_checkbox = CreateWindowExW(
            0, L"BUTTON", tr(nvidia_app_cleaner::TextId::settings_always_run_as_administrator),
            WS_CHILD | WS_TABSTOP | BS_AUTOCHECKBOX | BS_MULTILINE, 0, 0, 0, 0, window, nullptr,
            nullptr, nullptr);
        g_settings_status_label = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | SS_LEFT, 0, 0, 0, 0,
                                                  window, nullptr, nullptr, nullptr);
        g_settings_save_button = CreateWindowExW(
            0, L"BUTTON", tr(nvidia_app_cleaner::TextId::button_save),
            WS_CHILD | WS_TABSTOP | BS_DEFPUSHBUTTON, 0, 0, 0, 0, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSettingsSaveButtonId)), nullptr, nullptr);
        if (g_settings_title_label == nullptr || g_settings_description_label == nullptr ||
            g_always_admin_checkbox == nullptr || g_settings_status_label == nullptr ||
            g_settings_save_button == nullptr) {
            return -1;
        }

        create_and_apply_fonts(window);
        apply_theme(window);
        g_theme.watch(window, kSystemThemeChangedMessage);
        apply_localized_text(window);
        if (g_main_menu == nullptr) {
            return -1;
        }
        layout_controls(window);
        begin_scan(window);
        return 0;
    }

    case WM_SIZE:
        if (w_param != SIZE_MINIMIZED) {
            layout_controls(window);
        }
        return 0;

    case WM_INITMENU:
        update_navigation_state();
        return 0;

    case WM_GETMINMAXINFO: {
        auto *limits = reinterpret_cast<MINMAXINFO *>(l_param);
        if (limits == nullptr) {
            break;
        }

        RECT minimum{0, 0, scale_for_window(window, 880), scale_for_window(window, 520)};
        const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(window, GWL_STYLE));
        const DWORD extended_style = static_cast<DWORD>(GetWindowLongPtrW(window, GWL_EXSTYLE));
        const UINT dpi = GetDpiForWindow(window);
        if (AdjustWindowRectExForDpi(&minimum, style, GetMenu(window) != nullptr, extended_style,
                                     dpi != 0 ? dpi : static_cast<UINT>(USER_DEFAULT_SCREEN_DPI)) !=
            FALSE) {
            limits->ptMinTrackSize.x = minimum.right - minimum.left;
            limits->ptMinTrackSize.y = minimum.bottom - minimum.top;
        }
        return 0;
    }

    case WM_DPICHANGED: {
        scale_list_columns_for_dpi(LOWORD(w_param));
        const auto *suggested = reinterpret_cast<const RECT *>(l_param);
        if (suggested != nullptr) {
            SetWindowPos(window, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left, suggested->bottom - suggested->top,
                         SWP_NOACTIVATE | SWP_NOZORDER);
        }
        create_and_apply_fonts(window);
        layout_controls(window);
        return 0;
    }

    case WM_MEASUREITEM: {
        auto *item = reinterpret_cast<MEASUREITEMSTRUCT *>(l_param);
        if (item != nullptr && measure_menu_item(*item)) {
            return TRUE;
        }
        break;
    }

    case WM_DRAWITEM: {
        const auto *item = reinterpret_cast<DRAWITEMSTRUCT *>(l_param);
        if (item != nullptr && draw_menu_item(*item)) {
            return TRUE;
        }
        break;
    }

    case WM_NOTIFY: {
        const auto *header = reinterpret_cast<NMHDR *>(l_param);
        if (header != nullptr && header->code == NM_CUSTOMDRAW && is_app_button(header->hwndFrom)) {
            return g_theme.custom_draw_button(*reinterpret_cast<NMCUSTOMDRAW *>(l_param));
        }
        if (header != nullptr && header->hwndFrom == ListView_GetHeader(g_list_view)) {
            switch (header->code) {
            case HDN_BEGINTRACKA:
            case HDN_BEGINTRACKW: {
                const auto *column = reinterpret_cast<const NMHEADERW *>(l_param);
                if (g_resizing_list_columns) {
                    break;
                }
                if (column->iItem < 0 || column->iItem >= 3) {
                    return TRUE;
                }
                g_tracked_list_column = column->iItem;
                g_tracked_column_pair_width =
                    ListView_GetColumnWidth(g_list_view, column->iItem) +
                    ListView_GetColumnWidth(g_list_view, column->iItem + 1);
                break;
            }

            case HDN_TRACKA:
            case HDN_TRACKW:
            case HDN_ITEMCHANGINGA:
            case HDN_ITEMCHANGINGW: {
                auto *column = reinterpret_cast<NMHEADERW *>(l_param);
                const bool prevent_header_change =
                    header->code == HDN_ITEMCHANGINGA || header->code == HDN_ITEMCHANGINGW;
                const bool has_width =
                    column->pitem != nullptr && (column->pitem->mask & HDI_WIDTH) != 0;
                if (!g_resizing_list_columns && column->iItem == g_tracked_list_column &&
                    has_width) {
                    column->pitem->cxy =
                        constrained_tracked_column_width(window, column->iItem, column->pitem->cxy);
                    resize_tracked_column_pair(window, column->pitem->cxy);
                    return prevent_header_change ? TRUE : FALSE;
                }
                break;
            }

            case HDN_ENDTRACKA:
            case HDN_ENDTRACKW: {
                auto *column = reinterpret_cast<NMHEADERW *>(l_param);
                if (!g_resizing_list_columns && column->iItem == g_tracked_list_column) {
                    const bool has_width =
                        column->pitem != nullptr && (column->pitem->mask & HDI_WIDTH) != 0;
                    const int final_left_width = constrained_tracked_column_width(
                        window, column->iItem,
                        has_width ? column->pitem->cxy
                                  : ListView_GetColumnWidth(g_list_view, column->iItem));
                    if (has_width) {
                        column->pitem->cxy = final_left_width;
                    }
                    resize_tracked_column_pair(window, final_left_width);
                    g_tracked_list_column = -1;
                    g_tracked_column_pair_width = 0;
                }
                break;
            }

            case HDN_DIVIDERDBLCLICKA:
            case HDN_DIVIDERDBLCLICKW: {
                if (!g_resizing_list_columns) {
                    return TRUE;
                }
                break;
            }
            }
        }
        if (header != nullptr && header->hwndFrom == g_list_view && header->code == NM_CLICK) {
            const auto *activation = reinterpret_cast<const NMITEMACTIVATE *>(l_param);
            if (activation->iItem >= 0 &&
                static_cast<std::size_t>(activation->iItem) < g_displayed_cleanup_items.size()) {
                const auto &item = g_displayed_cleanup_items[activation->iItem];
                LVHITTESTINFO hit_test{};
                hit_test.pt = activation->ptAction;
                ListView_SubItemHitTest(g_list_view, &hit_test);
                if (is_parent_item(item) && (hit_test.flags & LVHT_ONITEMSTATEICON) == 0) {
                    g_scan_previous_selection = current_selection();
                    if (item.kind == CleanupItemKind::rollback_parent) {
                        g_rollback_drivers_expanded = !g_rollback_drivers_expanded;
                    } else {
                        g_optional_cleanup_expanded = !g_optional_cleanup_expanded;
                    }
                    render_scan_results(g_scan_previous_selection);
                    return 0;
                }
            }
        }
        if (header != nullptr && header->hwndFrom == g_list_view &&
            header->code == LVN_ITEMCHANGED && !g_refreshing_results) {
            const auto *change = reinterpret_cast<NMLISTVIEW *>(l_param);
            if (change->iItem >= 0 &&
                static_cast<std::size_t>(change->iItem) < g_displayed_cleanup_items.size() &&
                ((change->uOldState ^ change->uNewState) & LVIS_STATEIMAGEMASK) != 0) {
                const auto &item = g_displayed_cleanup_items[change->iItem];
                g_scan_previous_selection = current_selection();
                if (is_parent_item(item)) {
                    const bool selected = ((change->uNewState & LVIS_STATEIMAGEMASK) >> 12U) == 2U;
                    if (item.kind == CleanupItemKind::rollback_parent) {
                        select_all_driver_packages(selected);
                    } else {
                        select_all_optional_categories(selected);
                    }
                    render_scan_results(g_scan_previous_selection);
                    return 0;
                }

                g_refreshing_results = true;
                for (std::size_t row = 0; row < g_displayed_cleanup_items.size(); ++row) {
                    const auto &displayed = g_displayed_cleanup_items[row];
                    if (displayed.kind == CleanupItemKind::rollback_parent) {
                        ListView_SetCheckState(
                            g_list_view, static_cast<int>(row),
                            all_driver_packages_selected(g_scan_previous_selection) ? TRUE : FALSE);
                    } else if (displayed.kind == CleanupItemKind::optional_parent) {
                        ListView_SetCheckState(
                            g_list_view, static_cast<int>(row),
                            all_optional_categories_selected(g_scan_previous_selection) ? TRUE
                                                                                        : FALSE);
                    }
                }
                g_refreshing_results = false;
                update_selection_summary();
            }
        }
        break;
    }

    case WM_COMMAND:
        if (LOWORD(w_param) == kCleanupNavigationButtonId) {
            show_main_page(window);
            return 0;
        }
        if (LOWORD(w_param) == kRepairNavigationButtonId) {
            show_repair_page(window);
            return 0;
        }
        if (LOWORD(w_param) == kScanButtonId) {
            begin_scan(window);
            return 0;
        }
        if (LOWORD(w_param) == kCleanButtonId) {
            clean_selected(window);
            return 0;
        }
        if (LOWORD(w_param) == kRepairButtonId) {
            repair_stuck_downloads(window);
            return 0;
        }
        if (LOWORD(w_param) == kSelectRecommendedButtonId) {
            select_recommended_items();
            return 0;
        }
        if (LOWORD(w_param) == kClearSelectionButtonId) {
            clear_selected_items();
            return 0;
        }
        if (LOWORD(w_param) == kSettingsButtonId) {
            show_settings(window);
            return 0;
        }
        if (LOWORD(w_param) == kSettingsSaveButtonId) {
            save_settings();
            update_settings_status_text();
            return 0;
        }
        if (LOWORD(w_param) == IDM_LANGUAGE_ENGLISH ||
            LOWORD(w_param) == IDM_LANGUAGE_SIMPLIFIED_CHINESE) {
            g_language = LOWORD(w_param) == IDM_LANGUAGE_SIMPLIFIED_CHINESE
                             ? nvidia_app_cleaner::UiLanguage::simplified_chinese
                             : nvidia_app_cleaner::UiLanguage::english;
            apply_localized_text(window);
            return 0;
        }
        if (LOWORD(w_param) == IDM_HELP_ABOUT) {
            show_about_dialog(window);
            return 0;
        }
        if (LOWORD(w_param) == IDM_FILE_EXIT) {
            SendMessageW(window, WM_CLOSE, 0, 0);
            return 0;
        }
        break;

    case kScanCompletedMessage:
        finish_scan(window);
        return 0;

    case kSystemThemeChangedMessage:
    case WM_SETTINGCHANGE:
    case WM_SYSCOLORCHANGE:
    case WM_THEMECHANGED:
        apply_theme(window);
        static_cast<void>(replace_main_menu(window));
        update_navigation_state();
        return 0;

    case WM_ERASEBKGND: {
        RECT client{};
        GetClientRect(window, &client);
        FillRect(reinterpret_cast<HDC>(w_param), &client,
                 g_theme.background_brush() != nullptr ? g_theme.background_brush()
                                                       : GetSysColorBrush(COLOR_WINDOW));
        return 1;
    }

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORLISTBOX: {
        const HDC device_context = reinterpret_cast<HDC>(w_param);
        SetBkColor(device_context, g_theme.background_color());
        SetTextColor(device_context, g_theme.text_color());
        if (message == WM_CTLCOLORSTATIC) {
            SetBkMode(device_context, TRANSPARENT);
        }
        const HBRUSH brush = g_theme.background_brush() != nullptr ? g_theme.background_brush()
                                                                   : GetSysColorBrush(COLOR_WINDOW);
        return reinterpret_cast<LRESULT>(brush);
    }

    case WM_DESTROY:
        g_theme.stop_watching();
        if (g_scan_thread.joinable()) {
            g_scan_thread.request_stop();
            g_scan_thread.join();
        }
        {
            const std::scoped_lock lock(g_scan_mutex);
            g_pending_scan.reset();
        }
        if (g_main_menu != nullptr) {
            SetMenu(window, nullptr);
            DestroyMenu(g_main_menu);
            g_main_menu = nullptr;
        }
        g_menu_item_visuals.clear();
        if (g_menu_background_brush != nullptr) {
            DeleteObject(g_menu_background_brush);
            g_menu_background_brush = nullptr;
        }
        delete_fonts();
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(window, message, w_param, l_param);
}

} // namespace

namespace nvidia_app_cleaner::ui {

int run_elevated_repair_worker(UiLanguage language) {
    g_language = language;
    return run_elevated_repair();
}

int run_elevated_cleanup_worker(UiLanguage language, std::span<const CacheCategory> categories,
                                std::span<const DriverPackageId> driver_packages) {
    g_language = language;
    return run_elevated_cleanup(categories, driver_packages);
}

int run_main_window(HINSTANCE instance, int show_command, UiLanguage language) {
    g_language = language;

    INITCOMMONCONTROLSEX common_controls{
        .dwSize = sizeof(INITCOMMONCONTROLSEX),
        .dwICC = ICC_LISTVIEW_CLASSES,
    };
    InitCommonControlsEx(&common_controls);

    WNDCLASSEXW window_class{
        .cbSize = sizeof(WNDCLASSEXW),
        .style = CS_HREDRAW | CS_VREDRAW,
        .lpfnWndProc = window_proc,
        .hInstance = instance,
        .hIcon = LoadIconW(nullptr, IDI_APPLICATION),
        .hCursor = LoadCursorW(nullptr, IDC_ARROW),
        .hbrBackground = GetSysColorBrush(COLOR_WINDOW),
        .lpszClassName = kWindowClassName,
        .hIconSm = LoadIconW(nullptr, IDI_APPLICATION),
    };

    if (RegisterClassExW(&window_class) == 0) {
        return 1;
    }

    const HWND window = CreateWindowExW(
        0, kWindowClassName, tr(nvidia_app_cleaner::TextId::window_title), WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 980, 560, nullptr, nullptr, instance, nullptr);
    if (window == nullptr) {
        return 1;
    }

    ShowWindow(window, show_command);
    UpdateWindow(window);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    return static_cast<int>(message.wParam);
}

} // namespace nvidia_app_cleaner::ui
