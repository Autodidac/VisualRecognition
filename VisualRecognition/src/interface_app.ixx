module;
#define NOMINMAX
#include <windows.h>
#include "interface_ids.hpp"

#include <vector>
#include <string>
#include <optional>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <sstream>

export module interface.app;

import :state;
import :storage;
import :capture;
import :mouse_hooks;
import :layout;

import diagnostics.console_log;
import automation.macro.engine;
import automation.macro.hooks;

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

    if (!macro::load_macro())
    {
        consolelog::log("[MACRO] failed to load saved macro");
        SetStatus(L"Failed to load previous macro.");
    }
    else
    {
        std::size_t restored = 0;
        {
            std::lock_guard guard(macro::g_lock);
            restored = macro::g_macro.size();
        }

        std::ostringstream oss;
        oss << "[MACRO] restored " << restored << " event" << (restored == 1 ? "" : "s");
        consolelog::log(oss.str());

        std::wostringstream wss;
        wss << L"Restored " << static_cast<unsigned long long>(restored)
            << L" macro event" << (restored == 1 ? L"" : L"s");
        SetStatus(wss.str());
    }
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
