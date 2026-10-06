//
//  bdd_platform_win.cpp
//  Bass Daddy Devices VST3 glue
//
//  Windows side of bdd_platform.h. Ported from Graphite, where it ships; not yet built for YOI.
//

#include "bdd_platform.h"

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>

namespace bdd::vst3::platform {

void attachView(void* parent, void* child, int width, int height) {
    HWND parentWindow = static_cast<HWND>(parent);
    HWND childWindow = static_cast<HWND>(child);
    if (parentWindow == nullptr || childWindow == nullptr) {
        return;
    }
    // The web view starts life as a hidden popup; make it a child of the host's window instead.
    SetWindowLongPtrW(childWindow, GWL_STYLE, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
    SetParent(childWindow, parentWindow);
    SetWindowPos(childWindow, nullptr, 0, 0, width, height,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
}

void resizeView(void* child, int width, int height) {
    SetWindowPos(static_cast<HWND>(child), nullptr, 0, 0, width, height,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOMOVE);
}

void detachView(void* child) {
    HWND childWindow = static_cast<HWND>(child);
    ShowWindow(childWindow, SW_HIDE);
    SetParent(childWindow, nullptr);
}

std::filesystem::path productDirectory(const std::string& product) {
    std::filesystem::path base;
    PWSTR roaming = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &roaming)) && roaming != nullptr) {
        base = roaming;
    }
    CoTaskMemFree(roaming);
    return base / L"Bass Daddy Devices" / std::filesystem::path(std::u8string(product.begin(), product.end()));
}

void revealInFileBrowser(const std::filesystem::path& folder) {
    ShellExecuteW(nullptr, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

} // namespace bdd::vst3::platform
