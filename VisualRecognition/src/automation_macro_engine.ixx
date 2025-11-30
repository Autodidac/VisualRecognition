module;
#define NOMINMAX
#include <windows.h>

export module automation.macro.engine;

export import automation.macro.types;   // re-export control/types API

import std;
import diagnostics.console_log;

namespace macro
{
    inline HHOOK g_mouseHook = nullptr;
    inline HHOOK g_keyHook = nullptr;

    // ------------------------------------------------------------
    // Mouse hook
    // ------------------------------------------------------------
    export LRESULT CALLBACK mouse_proc(int code, WPARAM wp, LPARAM lp)
    {
        if (code < 0)
            return ::CallNextHookEx(nullptr, code, wp, lp);

        auto* m = reinterpret_cast<MSLLHOOKSTRUCT*>(lp);

        // Ignore injected events
        if (m->flags & (LLMHF_INJECTED | LLMHF_LOWER_IL_INJECTED))
            return ::CallNextHookEx(nullptr, code, wp, lp);

        const bool down = (wp == WM_LBUTTONDOWN);
        const bool up = (wp == WM_LBUTTONUP);

        if (!down && !up)
            return ::CallNextHookEx(nullptr, code, wp, lp);

        // If playing back a macro, ignore physical clicks
        if (g_play.load())
            return ::CallNextHookEx(nullptr, code, wp, lp);

        // If not recording, skip
        if (!g_record.load())
            return ::CallNextHookEx(nullptr, code, wp, lp);

        static bool lastDown = false;

        if (g_resetMouse.load())
        {
            lastDown = false;
            g_resetMouse.store(false);
        }

        const bool recDown = (down && !lastDown);
        const bool recUp = (up && lastDown);

        if (!recDown && !recUp)
            return ::CallNextHookEx(nullptr, code, wp, lp);

        const long long now = nowMs();
        const long long last = g_lastTick.load();
        const long long delta = (std::max)(0LL, now - last);
        g_lastTick.store(now);

        MouseEvent ev{};
        ev.x = m->pt.x;
        ev.y = m->pt.y;
        ev.down = recDown;
        ev.delay = static_cast<DWORD>(delta);

        capture_window_info(m->pt, ev);

        {
            std::lock_guard lock(g_lock);
            g_macro.push_back(ev);
        }

        std::ostringstream oss;
        oss << "[REC MOUSE] "
            << (ev.down ? "DOWN" : "UP")
            << " x=" << ev.x
            << " y=" << ev.y
            << " relX=" << ev.relX
            << " relY=" << ev.relY
            << " winW=" << ev.winW
            << " winH=" << ev.winH
            << " delay=" << ev.delay;
        consolelog::log(oss.str());

        if (recDown) lastDown = true;
        if (recUp)   lastDown = false;

        return ::CallNextHookEx(nullptr, code, wp, lp);
    }

    // ------------------------------------------------------------
    // Keyboard hook (F6–F9 + optional key recording)
    // ------------------------------------------------------------
    export LRESULT CALLBACK key_proc(int code, WPARAM wp, LPARAM lp)
    {
        if (code < 0)
            return ::CallNextHookEx(nullptr, code, wp, lp);

        auto* k = reinterpret_cast<KBDLLHOOKSTRUCT*>(lp);

        const bool down = (wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN);
        const bool up = (wp == WM_KEYUP || wp == WM_SYSKEYUP);

        // Control surface first (on key-up)
        if (up)
        {
            switch (k->vkCode)
            {
            case VK_F6:
                request_exit();
                break;
            case VK_F7:
                toggle_record();
                break;
            case VK_F8:
                clear_macro();
                break;
            case VK_F9:
                toggle_play();
                break;
            default:
                break;
            }
        }

        // If playing, don't record
        if (is_playing())
            return ::CallNextHookEx(nullptr, code, wp, lp);

        // If not recording, we are done
        if (!is_recording())
            return ::CallNextHookEx(nullptr, code, wp, lp);

        // Don't record our own control keys
        if (k->vkCode == VK_F6 || k->vkCode == VK_F7 ||
            k->vkCode == VK_F8 || k->vkCode == VK_F9)
            return ::CallNextHookEx(nullptr, code, wp, lp);

        if (!(down || up))
            return ::CallNextHookEx(nullptr, code, wp, lp);

        const long long now = nowMs();
        const long long last = g_lastTick.load();
        const long long delta = (std::max)(0LL, now - last);
        g_lastTick.store(now);

        KeyEvent ev{};
        ev.vk = static_cast<WORD>(k->vkCode);
        ev.down = down;
        ev.delay = static_cast<DWORD>(delta);

        {
            std::lock_guard lock(g_lock);
            g_macro.push_back(ev);
        }

        std::ostringstream oss;
        oss << "[REC KEY] vk=" << ev.vk
            << " down=" << (ev.down ? "1" : "0")
            << " delay=" << ev.delay;
        consolelog::log(oss.str());

        return ::CallNextHookEx(nullptr, code, wp, lp);
    }

    // ------------------------------------------------------------
    // Install / uninstall
    // ------------------------------------------------------------
    export bool install_hooks(HINSTANCE inst)
    {
        g_keyHook = ::SetWindowsHookExW(WH_KEYBOARD_LL, key_proc, inst, 0);
        g_mouseHook = ::SetWindowsHookExW(WH_MOUSE_LL, mouse_proc, inst, 0);
        return g_keyHook && g_mouseHook;
    }

    export void uninstall_hooks()
    {
        if (g_keyHook)
        {
            ::UnhookWindowsHookEx(g_keyHook);
            g_keyHook = nullptr;
        }
        if (g_mouseHook)
        {
            ::UnhookWindowsHookEx(g_mouseHook);
            g_mouseHook = nullptr;
        }
    }

} // namespace macro
