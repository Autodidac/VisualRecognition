module;
#define NOMINMAX
#include <windows.h>
#include "ids.hpp"

export module ui:hooks;

namespace ui
{
    namespace detail
    {
        using CaptureCallback = void(*)();

        inline HHOOK           g_uiMouseHook = nullptr;
        inline HWND            g_uiMainWindow = nullptr;
        inline CaptureCallback g_captureCallback = nullptr;

        // ---------------------------------------------------------------------
        // Corrected LL mouse hook
        // ---------------------------------------------------------------------
        LRESULT CALLBACK UiMouseProc(int code, WPARAM wp, LPARAM lp)
        {
            if (code < 0)
                return CallNextHookEx(g_uiMouseHook, code, wp, lp);

            auto* m = reinterpret_cast<MSLLHOOKSTRUCT*>(lp);

            // Ignore injected mouse events (script playback, automation, etc.)
            if (m->flags & (LLMHF_INJECTED | LLMHF_LOWER_IL_INJECTED))
                return CallNextHookEx(g_uiMouseHook, code, wp, lp);

            // Only activate capture on actual LButtonUP
            if (wp == WM_LBUTTONUP && g_captureCallback)
            {
                HWND underCursor = WindowFromPoint(m->pt);

                // Fix #1 — Only trigger if our app is foreground
                HWND fg = GetForegroundWindow();
                if (!fg || GetWindowThreadProcessId(fg, nullptr) != GetCurrentProcessId())
                    return CallNextHookEx(g_uiMouseHook, code, wp, lp);

                // Fix #2 — If the click is inside our main window OR a child, ignore it
                if (underCursor &&
                    (underCursor == g_uiMainWindow ||
                        IsChild(g_uiMainWindow, underCursor)))
                {
                    return CallNextHookEx(g_uiMouseHook, code, wp, lp);
                }

                // Fix #3 — Now perform capture (external click)
                g_captureCallback();
            }

            return CallNextHookEx(g_uiMouseHook, code, wp, lp);
        }
    }

    // ---------------------------------------------------------------------
    // Public API
    // ---------------------------------------------------------------------

    export void SetUiMainWindow(HWND hwnd) noexcept
    {
        detail::g_uiMainWindow = hwnd;
    }

    export void SetCaptureCallback(detail::CaptureCallback cb) noexcept
    {
        detail::g_captureCallback = cb;
    }

    export bool InstallUiMouseHook(HINSTANCE inst)
    {
        detail::g_uiMouseHook =
            SetWindowsHookExW(WH_MOUSE_LL, detail::UiMouseProc, inst, 0);

        return detail::g_uiMouseHook != nullptr;
    }

    export void UninstallUiMouseHook()
    {
        if (detail::g_uiMouseHook)
        {
            UnhookWindowsHookEx(detail::g_uiMouseHook);
            detail::g_uiMouseHook = nullptr;
        }
    }

} // namespace ui
