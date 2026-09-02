#include "ui/theme_manager.h"

#include <dwmapi.h>

#include <algorithm>
#include <iterator>
#include <string>

namespace nvidia_app_cleaner::ui {
namespace {

bool high_contrast_is_enabled() {
    HIGHCONTRASTW high_contrast{.cbSize = sizeof(HIGHCONTRASTW)};
    return SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(high_contrast), &high_contrast, 0) !=
               FALSE &&
           (high_contrast.dwFlags & HCF_HIGHCONTRASTON) != 0;
}

bool color_is_light(const winrt::Windows::UI::Color &color) {
    return ((5 * color.G) + (2 * color.R) + color.B) > (8 * 128);
}

bool system_prefers_dark_mode() {
    try {
        const auto settings = winrt::Windows::UI::ViewManagement::UISettings();
        const auto foreground =
            settings.GetColorValue(winrt::Windows::UI::ViewManagement::UIColorType::Foreground);
        return color_is_light(foreground);
    } catch (...) {
        return false;
    }
}

std::wstring window_text(HWND window) {
    const int length = GetWindowTextLengthW(window);
    if (length <= 0) {
        return {};
    }

    std::wstring result(static_cast<std::size_t>(length) + 1, L'\0');
    const int copied = GetWindowTextW(window, result.data(), length + 1);
    result.resize(copied > 0 ? static_cast<std::size_t>(copied) : 0);
    return result;
}

void frame_rectangle(HDC device_context, const RECT &rectangle, COLORREF color) {
    const HGDIOBJ previous_pen = SelectObject(device_context, GetStockObject(DC_PEN));
    const HGDIOBJ previous_brush = SelectObject(device_context, GetStockObject(NULL_BRUSH));
    SetDCPenColor(device_context, color);
    Rectangle(device_context, rectangle.left, rectangle.top, rectangle.right, rectangle.bottom);
    SelectObject(device_context, previous_brush);
    SelectObject(device_context, previous_pen);
}

} // namespace

ThemeManager::~ThemeManager() {
    stop_watching();
    if (background_brush_ != nullptr) {
        DeleteObject(background_brush_);
    }
}

void ThemeManager::apply(HWND window, HWND list_view) {
    const bool high_contrast = high_contrast_is_enabled();
    dark_mode_ = !high_contrast && system_prefers_dark_mode();
    background_color_ = high_contrast ? GetSysColor(COLOR_WINDOW)
                                      : (dark_mode_ ? RGB(32, 32, 32) : RGB(250, 250, 250));
    text_color_ = high_contrast ? GetSysColor(COLOR_WINDOWTEXT)
                                : (dark_mode_ ? RGB(240, 240, 240) : RGB(24, 24, 24));
    control_background_color_ = high_contrast ? GetSysColor(COLOR_WINDOW)
                                              : (dark_mode_ ? RGB(45, 45, 48) : RGB(255, 255, 255));

    const HBRUSH new_brush = CreateSolidBrush(background_color_);
    if (new_brush != nullptr) {
        const HBRUSH old_brush = background_brush_;
        background_brush_ = new_brush;
        SetClassLongPtrW(window, GCLP_HBRBACKGROUND, reinterpret_cast<LONG_PTR>(background_brush_));
        if (old_brush != nullptr) {
            DeleteObject(old_brush);
        }
    }

    const BOOL use_dark_title_bar = dark_mode_ ? TRUE : FALSE;
    DwmSetWindowAttribute(window, DWMWA_USE_IMMERSIVE_DARK_MODE, &use_dark_title_bar,
                          sizeof(use_dark_title_bar));

    if (list_view != nullptr) {
        ListView_SetBkColor(list_view, control_background_color_);
        ListView_SetTextBkColor(list_view, control_background_color_);
        ListView_SetTextColor(list_view, text_color_);
    }

    RedrawWindow(window, nullptr, nullptr,
                 RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN);
}

void ThemeManager::watch(HWND window, UINT notification_message) {
    stop_watching();
    try {
        ui_settings_.emplace();
        color_values_changed_token_ = ui_settings_->ColorValuesChanged(
            [window, notification_message](const auto &, const auto &) {
                PostMessageW(window, notification_message, 0, 0);
            });
        watching_system_theme_ = true;
    } catch (...) {
        ui_settings_.reset();
    }
}

void ThemeManager::stop_watching() {
    if (!watching_system_theme_ || !ui_settings_) {
        return;
    }
    try {
        ui_settings_->ColorValuesChanged(color_values_changed_token_);
    } catch (...) {
    }
    watching_system_theme_ = false;
    ui_settings_.reset();
}

bool ThemeManager::subclass_list_header(HWND header) {
    return header != nullptr && SetWindowSubclass(header, header_subclass_proc, 1,
                                                  reinterpret_cast<DWORD_PTR>(this)) != FALSE;
}

LRESULT ThemeManager::custom_draw_button(const NMCUSTOMDRAW &notification) const {
    if (!dark_mode_ || notification.dwDrawStage != CDDS_PREPAINT ||
        notification.hdr.hwndFrom == nullptr) {
        return CDRF_DODEFAULT;
    }

    const auto button_type = GetWindowLongPtrW(notification.hdr.hwndFrom, GWL_STYLE) & BS_TYPEMASK;
    if (button_type == BS_CHECKBOX || button_type == BS_AUTOCHECKBOX || button_type == BS_3STATE ||
        button_type == BS_AUTO3STATE) {
        paint_checkbox(notification);
    } else {
        paint_push_button(notification);
    }
    return CDRF_SKIPDEFAULT;
}

bool ThemeManager::dark_mode() const noexcept { return dark_mode_; }

COLORREF ThemeManager::background_color() const noexcept { return background_color_; }

COLORREF ThemeManager::control_background_color() const noexcept {
    return control_background_color_;
}

COLORREF ThemeManager::text_color() const noexcept { return text_color_; }

HBRUSH ThemeManager::background_brush() const noexcept { return background_brush_; }

void ThemeManager::paint_checkbox(const NMCUSTOMDRAW &notification) const {
    const HWND checkbox = notification.hdr.hwndFrom;
    const HDC device_context = notification.hdc;
    const RECT control_rect = notification.rc;
    const bool disabled =
        !IsWindowEnabled(checkbox) || (notification.uItemState & CDIS_DISABLED) != 0;

    SetDCBrushColor(device_context, background_color_);
    FillRect(device_context, &control_rect, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));

    const int control_height = control_rect.bottom - control_rect.top;
    const UINT dpi = GetDpiForWindow(checkbox);
    const int scaled_box_size = MulDiv(14, dpi != 0 ? static_cast<int>(dpi) : 96, 96);
    const int box_size = std::clamp(scaled_box_size, 10, std::max(10, control_height - 6));
    RECT box_rect{
        .left = control_rect.left + 2,
        .top = control_rect.top + ((control_height - box_size) / 2),
        .right = control_rect.left + 2 + box_size,
        .bottom = control_rect.top + ((control_height - box_size) / 2) + box_size,
    };

    const bool hot = (notification.uItemState & CDIS_HOT) != 0;
    SetDCBrushColor(device_context, hot ? RGB(62, 62, 66) : control_background_color_);
    FillRect(device_context, &box_rect, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    frame_rectangle(device_context, box_rect, disabled ? RGB(82, 82, 86) : RGB(132, 132, 136));

    if (SendMessageW(checkbox, BM_GETCHECK, 0, 0) == BST_CHECKED) {
        const HGDIOBJ previous_pen = SelectObject(device_context, GetStockObject(DC_PEN));
        SetDCPenColor(device_context, disabled ? RGB(120, 120, 124) : RGB(118, 185, 237));
        MoveToEx(device_context, box_rect.left + 3, box_rect.top + (box_size / 2), nullptr);
        LineTo(device_context, box_rect.left + (box_size / 2) - 1, box_rect.bottom - 4);
        LineTo(device_context, box_rect.right - 3, box_rect.top + 3);
        SelectObject(device_context, previous_pen);
    }

    const std::wstring label = window_text(checkbox);
    RECT text_rect{
        .left = box_rect.right + 8,
        .top = control_rect.top,
        .right = control_rect.right,
        .bottom = control_rect.bottom,
    };
    RECT measured_text{0, 0, std::max(0L, text_rect.right - text_rect.left), 0};
    const HFONT font = reinterpret_cast<HFONT>(SendMessageW(checkbox, WM_GETFONT, 0, 0));
    const HGDIOBJ previous_font =
        SelectObject(device_context, font != nullptr ? font : GetStockObject(DEFAULT_GUI_FONT));
    const int previous_background_mode = SetBkMode(device_context, TRANSPARENT);
    const COLORREF previous_text_color =
        SetTextColor(device_context, disabled ? RGB(140, 140, 144) : text_color_);
    DrawTextW(device_context, label.c_str(), static_cast<int>(label.size()), &measured_text,
              DT_CALCRECT | DT_LEFT | DT_NOPREFIX | DT_WORDBREAK);
    const int text_height = measured_text.bottom - measured_text.top;
    text_rect.top += std::max(0, (control_height - text_height) / 2);
    DrawTextW(device_context, label.c_str(), static_cast<int>(label.size()), &text_rect,
              DT_LEFT | DT_NOPREFIX | DT_WORDBREAK);
    SetTextColor(device_context, previous_text_color);
    SetBkMode(device_context, previous_background_mode);
    SelectObject(device_context, previous_font);

    if ((notification.uItemState & CDIS_FOCUS) != 0) {
        RECT focus_rect = text_rect;
        focus_rect.bottom = std::min(control_rect.bottom, focus_rect.top + text_height);
        DrawFocusRect(device_context, &focus_rect);
    }
}

void ThemeManager::paint_push_button(const NMCUSTOMDRAW &notification) const {
    const HWND button = notification.hdr.hwndFrom;
    const HDC device_context = notification.hdc;
    RECT control_rect = notification.rc;
    const bool disabled =
        !IsWindowEnabled(button) || (notification.uItemState & CDIS_DISABLED) != 0;
    const bool pressed = (notification.uItemState & CDIS_SELECTED) != 0;
    const bool hot = (notification.uItemState & CDIS_HOT) != 0;
    const bool focused = (notification.uItemState & CDIS_FOCUS) != 0;
    const bool checked = SendMessageW(button, BM_GETCHECK, 0, 0) == BST_CHECKED;
    const auto button_type = GetWindowLongPtrW(button, GWL_STYLE) & BS_TYPEMASK;
    const bool primary = button_type == BS_DEFPUSHBUTTON;

    COLORREF button_background =
        primary ? RGB(0, 120, 215) : (checked ? RGB(48, 58, 66) : control_background_color_);
    if (disabled) {
        button_background = RGB(38, 38, 40);
    } else if (pressed) {
        button_background = primary ? RGB(0, 84, 153) : RGB(72, 72, 76);
    } else if (hot) {
        button_background = primary ? RGB(16, 110, 190) : RGB(60, 60, 64);
    }
    SetDCBrushColor(device_context, button_background);
    FillRect(device_context, &control_rect, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));

    const COLORREF border = focused || primary || checked ? RGB(90, 180, 245) : RGB(92, 92, 96);
    frame_rectangle(device_context, control_rect, border);

    const std::wstring label = window_text(button);
    const HFONT font = reinterpret_cast<HFONT>(SendMessageW(button, WM_GETFONT, 0, 0));
    const HGDIOBJ previous_font =
        SelectObject(device_context, font != nullptr ? font : GetStockObject(DEFAULT_GUI_FONT));
    const int previous_background_mode = SetBkMode(device_context, TRANSPARENT);
    const COLORREF foreground =
        disabled ? RGB(140, 140, 144) : (primary ? RGB(255, 255, 255) : text_color_);
    const COLORREF previous_text_color = SetTextColor(device_context, foreground);
    if (pressed) {
        OffsetRect(&control_rect, 1, 1);
    }
    DrawTextW(device_context, label.c_str(), static_cast<int>(label.size()), &control_rect,
              DT_CENTER | DT_END_ELLIPSIS | DT_NOPREFIX | DT_SINGLELINE | DT_VCENTER);
    SetTextColor(device_context, previous_text_color);
    SetBkMode(device_context, previous_background_mode);
    SelectObject(device_context, previous_font);

    if (focused) {
        RECT focus_rect = notification.rc;
        InflateRect(&focus_rect, -3, -3);
        DrawFocusRect(device_context, &focus_rect);
    }
}

LRESULT CALLBACK ThemeManager::header_subclass_proc(HWND header, UINT message, WPARAM w_param,
                                                    LPARAM l_param, UINT_PTR subclass_id,
                                                    DWORD_PTR reference_data) {
    auto *manager = reinterpret_cast<ThemeManager *>(reference_data);
    if (manager != nullptr && manager->dark_mode() && message == WM_PAINT) {
        PAINTSTRUCT paint{};
        const HDC device_context = BeginPaint(header, &paint);
        manager->paint_header(header, device_context);
        EndPaint(header, &paint);
        return 0;
    }
    if (manager != nullptr && manager->dark_mode() && message == WM_PRINTCLIENT) {
        manager->paint_header(header, reinterpret_cast<HDC>(w_param));
        return 0;
    }
    if (manager != nullptr && manager->dark_mode() && message == WM_ERASEBKGND) {
        return 1;
    }
    if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(header, header_subclass_proc, subclass_id);
    }
    return DefSubclassProc(header, message, w_param, l_param);
}

void ThemeManager::paint_header(HWND header, HDC device_context) const {
    RECT client{};
    GetClientRect(header, &client);
    SetDCBrushColor(device_context, control_background_color_);
    FillRect(device_context, &client, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));

    const HFONT font = reinterpret_cast<HFONT>(SendMessageW(header, WM_GETFONT, 0, 0));
    const HGDIOBJ previous_font =
        SelectObject(device_context, font != nullptr ? font : GetStockObject(DEFAULT_GUI_FONT));
    const HGDIOBJ previous_pen = SelectObject(device_context, GetStockObject(DC_PEN));
    SetDCPenColor(device_context, RGB(78, 78, 82));
    SetBkMode(device_context, TRANSPARENT);
    SetTextColor(device_context, text_color_);

    const int item_count = Header_GetItemCount(header);
    for (int index = 0; index < item_count; ++index) {
        RECT item_rect{};
        if (Header_GetItemRect(header, index, &item_rect) == FALSE) {
            continue;
        }
        wchar_t title[256]{};
        HDITEMW item{
            .mask = HDI_TEXT | HDI_FORMAT,
            .pszText = title,
            .cchTextMax = static_cast<int>(std::size(title)),
        };
        Header_GetItem(header, index, &item);

        MoveToEx(device_context, item_rect.right - 1, item_rect.top, nullptr);
        LineTo(device_context, item_rect.right - 1, item_rect.bottom);
        MoveToEx(device_context, item_rect.left, item_rect.bottom - 1, nullptr);
        LineTo(device_context, item_rect.right, item_rect.bottom - 1);

        RECT text_rect = item_rect;
        text_rect.left += 7;
        text_rect.right -= 7;
        UINT alignment = DT_LEFT;
        if ((item.fmt & HDF_CENTER) != 0) {
            alignment = DT_CENTER;
        } else if ((item.fmt & HDF_RIGHT) != 0) {
            alignment = DT_RIGHT;
        }
        DrawTextW(device_context, title, -1, &text_rect,
                  alignment | DT_END_ELLIPSIS | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    }

    SelectObject(device_context, previous_pen);
    SelectObject(device_context, previous_font);
}

} // namespace nvidia_app_cleaner::ui
