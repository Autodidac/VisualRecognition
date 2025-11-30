module;
#define NOMINMAX
#include <windows.h>

#include "ids.hpp"

export module ui:hooks;

import :common;
import :capture;

namespace ui::detail
{
    // GLOBAL MOUSE HOOK — captures anywhere even when app is not focused
    LRESULT CALLBACK MouseHookProc(int code, WPARAM wParam, LPARAM lParam)
    {
        if (code >= 0)
        {
            if (g_mouseHookPaused)
                return ::CallNextHookEx(g_mouseHook, code, wParam, lParam);

            auto belongsToMainWindow = [](HWND hwnd)
            {
                if (!hwnd || !g_mainWindow)
                    return false;

                HWND root = ::GetAncestor(hwnd, GA_ROOT);
                return root == g_mainWindow;
            };

            MSLLHOOKSTRUCT* info = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);
            HWND hitWindow = info ? ::WindowFromPoint(info->pt) : nullptr;
            HWND foreground = ::GetForegroundWindow();

            if (belongsToMainWindow(foreground) || belongsToMainWindow(hitWindow))
                return ::CallNextHookEx(g_mouseHook, code, wParam, lParam);

            switch (wParam)
            {
            case WM_LBUTTONDOWN:
                DoCapture();
                break;
            }
        }

        return ::CallNextHookEx(g_mouseHook, code, wParam, lParam);
    }

    // GLOBAL KEYBOARD HOOK — F5 triggers classify even when unfocused
    LRESULT CALLBACK KeyboardHookProc(int code, WPARAM wParam, LPARAM lParam)
    {
        if (code >= 0)
        {
            if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)
            {
                auto* kbd = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
                if (kbd && kbd->vkCode == VK_F5)
                {
                    DoClassify();
                }
            }
        }

        return ::CallNextHookEx(g_keyboardHook, code, wParam, lParam);
    }
}
