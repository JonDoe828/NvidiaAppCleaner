#include "nvidia_app_cleaner/localization.h"

#include <array>

#ifdef _WIN32
#include "resource.h"

#include <windows.h>

#include <string>
#else
namespace nvidia_app_cleaner {
namespace {

using TextTable = std::array<const wchar_t *, static_cast<std::size_t>(TextId::count)>;

constexpr TextTable kEnglish{
    L"Nvidia App Cleaner",
    L"Unknown error",
    L"Windows error ",
    L"NVIDIA LocalSystem Container is not in a repairable service state.",
    L"Timed out while changing the NVIDIA service state.",
    L"Game Ready Driver",
    L"Studio Driver",
    L"Game Ready / rollback driver packages",
    L"Studio / rollback driver packages",
    L"NVIDIA App update packages",
    L"Legacy NVIDIA App update cache",
    L"Legacy driver downloader cache",
    L"Category",
    L"Location",
    L"Size",
    L"Status",
    L"Found",
    L"Empty",
    L"Not found",
    L"Access error",
    L"Unsafe path",
    L"Partial",
    L"Unknown",
    L"Scan again",
    L"Repair stuck 100%",
    L"Settings",
    L"Save",
    L"Back",
    L"Read-only preview — scanning...",
    L"Windows ProgramData location could not be found.",
    L"Read-only — ",
    L" found. No files changed.",
    L" A stuck 100% download is repairable.",
    L"NVIDIA App is still running. Exit it completely and start the repair again. No files were "
    L"changed.",
    L"NVIDIA LocalSystem Container could not be paused, so no JSON files were changed.\n\n",
    L"NVIDIA App UpdateFramework location could not be found.",
    L"Local AppData location could not be found.",
    L"Backup: ",
    L"NVIDIA service restart: ",
    L"The stale state disappeared before repair. No JSON files were changed.",
    L"Repair was not completed for:\n\n",
    L"\nAny successful repair is listed below:\n",
    L"The stale download record was removed. Reopen NVIDIA App to download the driver again.\n\n",
    L"Nvidia App Cleaner could not locate its executable.",
    L"The repair process could not be started.\n\n",
    L"Exit NVIDIA App completely from its system-tray menu, then try the repair again. No files "
    L"were changed.",
    L"No completed driver download with an empty or missing fileLocation was found. No files were "
    L"changed.",
    L"The following completed download records point to a missing installer:\n\n",
    L"\nNvidia App Cleaner will back up the JSON files and remove only the stale taskId records. "
    L"Continue?",
    L"Repair stuck 100% download",
    L"Startup permissions",
    L"Scanning does not need administrator privileges. Cleanup and repair request UAC only when "
    L"started. Enable this option only if you want every normal launch elevated.",
    L"Always launch Nvidia App Cleaner as administrator",
    L"The setting was saved and will take effect on the next launch.",
    L"The startup setting could not be read. Nvidia App Cleaner will continue without automatic "
    L"elevation. ",
    L"The startup setting could not be saved. ",
    L"Nvidia App Cleaner could not restart with administrator privileges.\n\n",
    L"Installer2 maintenance cache",
    L"NGX downloaded models",
    L"Clean selected",
    L"Windows Program Files location could not be found.",
    L"Select at least one non-empty, readable item to clean.",
    L"The following contents will be permanently deleted:\n\n",
    L"\nNvidia App Cleaner will request administrator approval and rescan after cleanup. Continue?",
    L"\nDriver package warning: this removes downloaded or rollback installers. A pending driver "
    L"must "
    L"be downloaded again.\n",
    L"\nInstaller2 warning: current drivers keep working, but an older driver rollback may not "
    L"reinstall all matching add-ons.\n",
    L"\nNGX warning: games or NVIDIA features may download these AI models again.\n",
    L"Clean selected NVIDIA files",
    L"The elevated cleanup process could not be started.\n\n",
    L"Cleanup completed successfully.\n\n",
    L"Cleanup completed with some failures.\n\n",
    L"Cleanup failed. No selected category was fully cleaned.\n\n",
    L"Removed ",
    L".\n",
    L"Exit NVIDIA App completely from its system-tray menu, then start cleanup again. No files "
    L"were changed.",
    L"NVIDIA LocalSystem Container could not be paused, so NVIDIA App cache items were not "
    L"cleaned.\n\n",
    L"No files needed to be removed.",
    L"Storage cleanup",
    L"Review found files before cleaning. Rollback, Installer2, and NGX items stay optional.",
    L"Select recommended",
    L"Clear selection",
    L"Available: ",
    L"Selected: ",
    L"Scan complete. No files were changed.",
    L"Current NVIDIA driver: reading...",
    L"Current NVIDIA driver: ",
    L"Current NVIDIA driver: not detected",
    L"Driver package: ",
    L"Download repair",
    L"Repairs only NVIDIA App records that are stuck at 100% after a completed driver installer "
    L"was lost. It never runs automatically or cleans other files.",
    L"No broken download record was found.",
    L"Repairable records: ",
    L"Recommended cleanup (selected by default)",
    L"Optional cleanup",
    L"Rollback drivers",
};

constexpr TextTable kSimplifiedChinese{
    L"Nvidia App Cleaner",
    L"未知错误",
    L"Windows 错误 ",
    L"NVIDIA LocalSystem Container 服务当前状态不允许修复。",
    L"等待 NVIDIA 服务切换状态超时。",
    L"Game Ready 驱动",
    L"Studio 驱动",
    L"Game Ready / 回滚驱动包",
    L"Studio / 回滚驱动包",
    L"NVIDIA App 更新包",
    L"旧版 NVIDIA App 更新缓存",
    L"旧版驱动下载缓存",
    L"类别",
    L"位置",
    L"大小",
    L"状态",
    L"已找到",
    L"空",
    L"不存在",
    L"无法访问",
    L"路径不安全",
    L"部分可读",
    L"未知",
    L"重新扫描",
    L"修复卡在 100%",
    L"设置",
    L"保存",
    L"返回",
    L"只读预览 — 正在扫描……",
    L"无法找到 Windows ProgramData 目录。",
    L"只读 — 检测到 ",
    L"。未修改文件。",
    L" 检测到可修复的 100% 卡住下载。",
    L"NVIDIA App 仍在运行。请完全退出后重新执行修复。未修改任何文件。",
    L"无法暂停 NVIDIA LocalSystem Container，因此未修改任何 JSON 文件。\n\n",
    L"无法找到 NVIDIA App UpdateFramework 目录。",
    L"无法找到本地 AppData 目录。",
    L"备份：",
    L"重启 NVIDIA 服务：",
    L"执行修复前异常状态已经消失，未修改任何 JSON 文件。",
    L"以下项目未能完成修复：\n\n",
    L"\n已成功修复的项目：\n",
    L"已移除失效的下载记录。请重新打开 NVIDIA App 下载驱动。\n\n",
    L"Nvidia App Cleaner 无法定位自身程序文件。",
    L"无法启动修复进程。\n\n",
    L"请从系统托盘完全退出 NVIDIA App，然后重新执行修复。未修改任何文件。",
    L"没有找到下载已完成但 fileLocation 为空或文件不存在的记录。未修改任何文件。",
    L"以下已完成的下载记录指向不存在的安装包：\n\n",
    L"\nNvidia App Cleaner 将先备份 JSON，然后只移除失效 taskId 对应的记录。是否继续？",
    L"修复卡在 100% 的下载",
    L"启动权限",
    L"扫描不需要管理员权限。只有开始执行清理或修复时才会请求 "
    L"UAC。仅当你希望每次正常启动都使用管理员权限时，才启用此选项。",
    L"始终以管理员身份启动 Nvidia App Cleaner",
    L"设置已保存，将在下次启动时生效。",
    L"无法读取启动设置。Nvidia App Cleaner 将不自动提权并继续运行。",
    L"无法保存启动设置。",
    L"Nvidia App Cleaner 无法使用管理员权限重新启动。\n\n",
    L"Installer2 安装维护缓存",
    L"NGX 已下载模型",
    L"清理所选项",
    L"无法找到 Windows Program Files 目录。",
    L"请至少选择一个非空且可读取的清理项目。",
    L"将永久删除以下内容：\n\n",
    L"\nNvidia App Cleaner 将请求管理员权限，并在清理后重新扫描。是否继续？",
    L"\n驱动包警告：这会删除已下载或用于回滚的安装包。尚未安装的驱动需要重新下载。\n",
    L"\nInstaller2 "
    L"警告：当前驱动仍可使用，但回滚旧驱动时可能无法完整安装对应附加组件。\n",
    L"\nNGX 警告：游戏或 NVIDIA 功能之后可能重新下载这些 AI 模型。\n",
    L"清理所选 NVIDIA 文件",
    L"无法启动管理员清理进程。\n\n",
    L"清理已完成。\n\n",
    L"清理已完成，但部分项目失败。\n\n",
    L"清理失败，没有任何所选项目被完整清理。\n\n",
    L"已删除 ",
    L"。\n",
    L"请从系统托盘完全退出 NVIDIA App，然后重新执行清理。未修改任何文件。",
    L"无法暂停 NVIDIA LocalSystem Container，因此未清理 NVIDIA App 缓存项目。\n\n",
    L"没有需要删除的文件。",
    L"存储清理",
    L"清理前先检查扫描结果。回滚驱动、Installer2 和 NGX 项目始终保持可选。",
    L"选择建议项",
    L"清空选择",
    L"可清理：",
    L"已选择：",
    L"扫描完成，未修改任何文件。",
    L"当前 NVIDIA 驱动：正在读取……",
    L"当前 NVIDIA 驱动：",
    L"当前 NVIDIA 驱动：未检测到",
    L"驱动安装包：",
    L"下载修复",
    L"仅修复 NVIDIA App 已记录下载完成、但驱动安装包丢失而卡在 100% "
    L"的记录。不会自动运行，也不会清理其他文件。",
    L"没有检测到异常下载记录。",
    L"可修复记录：",
    L"建议清理（默认勾选）",
    L"可选清理",
    L"回滚驱动",
};

static_assert(kEnglish.size() == kSimplifiedChinese.size());

} // namespace
} // namespace nvidia_app_cleaner
#endif

namespace nvidia_app_cleaner {

#ifdef _WIN32
namespace {

using TextTable = std::array<std::wstring, static_cast<std::size_t>(TextId::count)>;

bool select_thread_language(UiLanguage language) {
    thread_local bool language_selected = false;
    thread_local UiLanguage selected_language = UiLanguage::english;
    if (language_selected && selected_language == language) {
        return true;
    }

    ULONG language_count = 0;
    const wchar_t *language_names =
        language == UiLanguage::simplified_chinese ? L"zh-CN\0" : L"en-US\0";
    if (SetThreadPreferredUILanguages(MUI_LANGUAGE_NAME, language_names, &language_count) ==
            FALSE ||
        language_count == 0) {
        return false;
    }

    language_selected = true;
    selected_language = language;
    return true;
}

std::wstring load_resource_string(std::size_t index) {
    const UINT resource_id = IDS_TEXT_BASE + static_cast<UINT>(index);
    wchar_t *resource_text = nullptr;
    const int length = LoadStringW(GetModuleHandleW(nullptr), resource_id,
                                   reinterpret_cast<LPWSTR>(&resource_text), 0);
    return length > 0 && resource_text != nullptr ? std::wstring(resource_text, length)
                                                  : std::wstring{};
}

TextTable load_resource_table() {
    TextTable table;
    for (std::size_t index = 0; index < table.size(); ++index) {
        table[index] = load_resource_string(index);
    }
    return table;
}

const TextTable &resource_table(UiLanguage language) {
    if (language == UiLanguage::simplified_chinese) {
        static const TextTable simplified_chinese = load_resource_table();
        return simplified_chinese;
    }

    static const TextTable english = load_resource_table();
    return english;
}

} // namespace
#endif

UiLanguage system_ui_language() {
#ifdef _WIN32
    if (PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_CHINESE) {
        return UiLanguage::simplified_chinese;
    }
#endif
    return UiLanguage::english;
}

const wchar_t *text(UiLanguage language, TextId id) {
    const auto index = static_cast<std::size_t>(id);
    if (index >= static_cast<std::size_t>(TextId::count)) {
        return L"";
    }
#ifdef _WIN32
    static_cast<void>(select_thread_language(language));
    const auto &selected = resource_table(language);
    if (!selected[index].empty()) {
        return selected[index].c_str();
    }

    static_cast<void>(select_thread_language(UiLanguage::english));
    const auto &english = resource_table(UiLanguage::english);
    static_cast<void>(select_thread_language(language));
    return english[index].c_str();
#else
    return language == UiLanguage::simplified_chinese ? kSimplifiedChinese[index] : kEnglish[index];
#endif
}

} // namespace nvidia_app_cleaner
