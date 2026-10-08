//
//  bdd_platform.h
//  Bass Daddy Devices VST3 glue
//
//  The few things that differ per operating system: putting the web view inside the host's
//  window, and where a synth's own files (presets, drawings) live. Shared by every synth.
//
//  Implemented in bdd_platform_mac.mm and bdd_platform_win.cpp.
//

#pragma once

#include <filesystem>
#include <string>

namespace bdd::vst3::platform {

/// Makes `child` (the web view's native view) a subview of the host-supplied `parent`, filling it.
void attachView(void* parent, void* child, int width, int height);

void resizeView(void* child, int width, int height);

/// Takes `child` back out of the host's window before the web view is destroyed.
void detachView(void* child);

/// The synth's folder for its own files:
///   macOS    ~/Library/Application Support/Bass Daddy Devices/<product>
///   Windows  %APPDATA%\Bass Daddy Devices\<product>
/// The Audio Unit uses the same layout inside its sandbox container, so its files are the same
/// format but not the same files.
std::filesystem::path productDirectory(const std::string& product);

/// Where Bass Daddy Devices licenses are kept, shared by every synth (BDDLicenseStore.hpp):
/// `Bass Daddy Devices/Licenses` beside the synths' own folders.
inline std::filesystem::path licensesDirectory() {
    return productDirectory("Licenses");
}

/// The clipboard's text (UTF-8), or empty. Hosts route paste shortcuts to their own Edit menu,
/// so the editor reads the clipboard through this instead.
std::string clipboardText();

/// Opens `folder` in Finder or Explorer.
void revealInFileBrowser(const std::filesystem::path& folder);

} // namespace bdd::vst3::platform
