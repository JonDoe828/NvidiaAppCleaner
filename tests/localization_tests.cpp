#include "nvidia_app_cleaner/localization.h"

#include <iostream>

int main() {
    using nvidia_app_cleaner::TextId;
    using nvidia_app_cleaner::UiLanguage;

    for (std::size_t index = 0; index < static_cast<std::size_t>(TextId::count); ++index) {
        const auto id = static_cast<TextId>(index);
        if (nvidia_app_cleaner::text(UiLanguage::english, id)[0] == L'\0' ||
            nvidia_app_cleaner::text(UiLanguage::simplified_chinese, id)[0] == L'\0') {
            std::cerr << "Localization entry " << index << " is empty\n";
            return 1;
        }
    }

    if (std::wstring(nvidia_app_cleaner::text(UiLanguage::english, TextId::window_title)) !=
            L"Nvidia App Cleaner" ||
        std::wstring(nvidia_app_cleaner::text(UiLanguage::simplified_chinese,
                                              TextId::window_title)) != L"Nvidia App Cleaner") {
        std::cerr << "Window title should use separated words\n";
        return 1;
    }

    if (std::wstring(nvidia_app_cleaner::text(UiLanguage::english, TextId::button_scan_again)) ==
        nvidia_app_cleaner::text(UiLanguage::simplified_chinese, TextId::button_scan_again)) {
        std::cerr << "English and Chinese scan labels should differ\n";
        return 1;
    }

    const std::wstring english_before =
        nvidia_app_cleaner::text(UiLanguage::english, TextId::button_scan_again);
    const std::wstring simplified_chinese =
        nvidia_app_cleaner::text(UiLanguage::simplified_chinese, TextId::button_scan_again);
    const std::wstring english_after =
        nvidia_app_cleaner::text(UiLanguage::english, TextId::button_scan_again);
    if (english_before != L"Scan again" || simplified_chinese != L"重新扫描" ||
        english_after != english_before) {
        std::cerr << "Runtime language switching returned the wrong resource\n";
        return 1;
    }

    if (std::wstring(nvidia_app_cleaner::text(UiLanguage::english, TextId::navigation_repair)) !=
            L"Download repair" ||
        std::wstring(nvidia_app_cleaner::text(UiLanguage::simplified_chinese,
                                              TextId::navigation_repair)) != L"下载修复") {
        std::cerr << "Navigation labels returned the wrong localized text\n";
        return 1;
    }

    if (std::wstring(nvidia_app_cleaner::text(
            UiLanguage::english, TextId::cleanup_group_optional)) != L"Optional cleanup" ||
        std::wstring(nvidia_app_cleaner::text(UiLanguage::simplified_chinese,
                                              TextId::cleanup_group_rollback_drivers)) !=
            L"回滚驱动") {
        std::cerr << "Cleanup group labels returned the wrong localized text\n";
        return 1;
    }

    std::cout << "All localization tests passed\n";
    return 0;
}
