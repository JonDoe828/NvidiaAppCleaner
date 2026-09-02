#pragma once

#include <windows.h>

#include <commctrl.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.ViewManagement.h>

#include <optional>

namespace nvidia_app_cleaner::ui {

class ThemeManager {
  public:
    ThemeManager() = default;
    ThemeManager(const ThemeManager &) = delete;
    ThemeManager &operator=(const ThemeManager &) = delete;
    ~ThemeManager();

    void apply(HWND window, HWND list_view);
    void watch(HWND window, UINT notification_message);
    void stop_watching();
    [[nodiscard]] bool subclass_list_header(HWND header);
    [[nodiscard]] LRESULT custom_draw_button(const NMCUSTOMDRAW &notification) const;

    [[nodiscard]] bool dark_mode() const noexcept;
    [[nodiscard]] COLORREF background_color() const noexcept;
    [[nodiscard]] COLORREF control_background_color() const noexcept;
    [[nodiscard]] COLORREF text_color() const noexcept;
    [[nodiscard]] HBRUSH background_brush() const noexcept;

  private:
    static LRESULT CALLBACK header_subclass_proc(HWND header, UINT message, WPARAM w_param,
                                                 LPARAM l_param, UINT_PTR subclass_id,
                                                 DWORD_PTR reference_data);
    void paint_checkbox(const NMCUSTOMDRAW &notification) const;
    void paint_push_button(const NMCUSTOMDRAW &notification) const;
    void paint_header(HWND header, HDC device_context) const;

    HBRUSH background_brush_{nullptr};
    COLORREF background_color_{RGB(255, 255, 255)};
    COLORREF control_background_color_{RGB(255, 255, 255)};
    COLORREF text_color_{RGB(0, 0, 0)};
    bool dark_mode_{false};
    std::optional<winrt::Windows::UI::ViewManagement::UISettings> ui_settings_;
    winrt::event_token color_values_changed_token_{};
    bool watching_system_theme_{false};
};

} // namespace nvidia_app_cleaner::ui
