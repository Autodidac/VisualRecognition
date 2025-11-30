module;
#define NOMINMAX
#include <windows.h>

export module ui;

// Import partitions of this same module
import :common;
import :filesystem;
import :capture;
import :hooks;
import :layout;

// External modules
import vr.console_log;
import vr.macro.core;
import vr.macro.hooks;

export void RunUI(HINSTANCE inst, int show)
{
    // DO NOT: using namespace ui::detail;
    // DO NOT open namespace ui::detail here.

    // Load existing AI model if present
    ui::detail::LoadModel(ui::detail::g_ai);

    // Create and show the main window
    HWND hwnd = ui::detail::CreateAndShowMainWindow(inst, show);
    if (!hwnd)
        return;

    // Wire UI hooks: main window + capture callback
    ui::SetUiMainWindow(hwnd);
    ui::SetCaptureCallback(+[]()
        {
            ui::detail::DoCapture();
        });

    // Start macro playback and global macro hooks
    macro::start_playback_thread();
    macro::install_hooks(inst);

    // Install UI low-level mouse hook (click-to-capture)
    ui::InstallUiMouseHook(inst);

    // Standard message loop
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // Cleanup on exit
    ui::UninstallUiMouseHook();
    macro::uninstall_hooks();
}
