module;
#define NOMINMAX
#include <windows.h>
#include "ids.hpp"

#include <vector>
#include <string>
#include <optional>
#include <chrono>
#include <filesystem>

export module ui;

import :common;
import :filesystem;
import :capture;
import :hooks;
import :layout;

import vr.console_log;
import vr.macro.core;
import vr.macro.hooks;

export void RunUI(HINSTANCE inst, int show)
{
    using namespace ui::detail;

    // Load model if it exists
    LoadModel(g_ai);

    // Create main UI
    HWND hwnd = CreateAndShowMainWindow(inst, show);
    if (!hwnd)
        return;

    // Start macro engine
    macro::start_playback_thread();
    macro::install_hooks(inst);

    // Install UI low-level mouse hook (click-to-capture)
    ui::InstallUiMouseHook(inst);

    // Message loop
    MSG msg{};
    while (::GetMessageW(&msg, nullptr, 0, 0))
    {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }

    ui::UninstallUiMouseHook();
    macro::uninstall_hooks();
}
