module;
#define NOMINMAX
#include <windows.h>
#include "ids.hpp"

export module ui:hooks;

import :common;
import std;

namespace ui
{
    namespace detail
    {
        inline HHOOK g_uiMouseHook = nullptr;
        extern HWND g_mainWindow;        // defined in ui:common
        extern std::atomic_bool g_isCapturing;

        // Helper: determine if a window belongs to our UI
        // (catches theme child windows, compositor layers, owner-draw wrappers)
        static bool IsInUi(HWND h)
        {
            while (h)
            {
                if (h == ui::detail::g_mainWindow)
                    return true;
                h = ::GetParent(h);
            }
            return false;
        }

        // --------------------------------------------------------------------
        // LOW-LEVEL MOUSE HOOK
        // --------------------------------------------------------------------
        LRESULT CALLBACK UiMouseProc(int code, WPARAM wp, LPARAM lp)
        {
            if (code < 0)
                return ::CallNextHookEx(g_uiMouseHook, code, wp, lp);

            auto* m = reinterpret_cast<MSLLHOOKSTRUCT*>(lp);

            // Ignore artificial events (SendInput, synthesized clicks)
            if (m->flags & (LLMHF_INJECTED | LLMHF_LOWER_IL_INJECTED))
                return ::CallNextHookEx(g_uiMouseHook, code, wp, lp);

            // Disable hook-triggered capture during DoCapture
            if (ui::detail::g_isCapturing.load(std::memory_order_relaxed))
                return ::CallNextHookEx(g_uiMouseHook, code, wp, lp);

            // Only check real left-button release
            if (wp == WM_LBUTTONUP && ui::detail::g_mainWindow)
            {
                HWND underCursor = ::WindowFromPoint(m->pt);

                // If cursor is inside *any* UI descendant → skip capture
                if (underCursor && IsInUi(underCursor))
                    return ::CallNextHookEx(g_uiMouseHook, code, wp, lp);

                // Explicit control IDs that must never trigger capture
                static constexpr int kSkipIds[] =
                {
                    IDC_BTN_PREV, IDC_BTN_NEXT, IDC_BTN_DELETE, IDC_BTN_CLASSIFY,
                    IDC_BTN_LEARN, IDC_BTN_CAPTURE, IDC_BTN_PROMPT,
                    IDC_BTN_CLEAR_HISTORY, IDC_BTN_EXIT,
                    IDC_MOUSE_COORDS,
                    IDC_STATUS, IDC_HISTORY, IDC_PREVIEW, IDC_LOG_EDIT
                };

                // Check if the cursor is over these controls or any of their children
                for (int id : kSkipIds)
                {
                    HWND h = ::GetDlgItem(ui::detail::g_mainWindow, id);
                    if (!h)
                        continue;

                    // Direct hit on control
                    if (underCursor == h)
                        return ::CallNextHookEx(g_uiMouseHook, code, wp, lp);

                    // Check ancestry chain: underCursor child → control parent
                    HWND parent = underCursor;
                    while (parent)
                    {
                        if (parent == h)
                            return ::CallNextHookEx(g_uiMouseHook, code, wp, lp);
                        parent = ::GetParent(parent);
                    }
                }

                // ------------------------------
                // Outside-click confirmed → CAPTURE
                // ------------------------------
                ::PostMessageW(ui::detail::g_mainWindow, WM_USER + 101, 0, 0);

                return ::CallNextHookEx(g_uiMouseHook, code, wp, lp);
            }

            return ::CallNextHookEx(g_uiMouseHook, code, wp, lp);
        }

    } // namespace detail

    // --------------------------------------------------------------------
    // PUBLIC API
    // --------------------------------------------------------------------

    export void SetUiMainWindow(HWND hwnd) noexcept
    {
        detail::g_mainWindow = hwnd;
    }

    export bool InstallUiMouseHook(HINSTANCE inst)
    {
        detail::g_uiMouseHook =
            ::SetWindowsHookExW(WH_MOUSE_LL, detail::UiMouseProc, inst, 0);

        return (detail::g_uiMouseHook != nullptr);
    }

    export void UninstallUiMouseHook()
    {
        if (detail::g_uiMouseHook)
        {
            ::UnhookWindowsHookEx(detail::g_uiMouseHook);
            detail::g_uiMouseHook = nullptr;
        }
    }

} // namespace ui
