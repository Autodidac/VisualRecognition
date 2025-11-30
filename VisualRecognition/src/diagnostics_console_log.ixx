module;
#define NOMINMAX
#include <windows.h>

export module diagnostics.console_log;

import std;

namespace consolelog
{
    // Custom message for log flush into UI edit control
    export inline constexpr UINT WM_LOG_FLUSH = WM_APP + 42;

    // Internal state – single definition here
    std::mutex               g_mutex;
    std::vector<std::string> g_queue;
    HWND                     g_host = nullptr;
    HWND                     g_edit = nullptr;

    // ------------------------------------------------------------
    // Internal helper: append UTF-8 line to EDIT control
    // ------------------------------------------------------------
    void append_to_edit_locked(std::string_view line)
    {
        if (!g_edit)
            return;

        int wideLen = ::MultiByteToWideChar(
            CP_UTF8, 0,
            line.data(),
            static_cast<int>(line.size()),
            nullptr, 0);

        if (wideLen <= 0)
            return;

        std::wstring wline(static_cast<std::size_t>(wideLen), L'\0');
        ::MultiByteToWideChar(
            CP_UTF8, 0,
            line.data(),
            static_cast<int>(line.size()),
            wline.data(),
            wideLen);

        wline.append(L"\r\n");

        const int existing = ::GetWindowTextLengthW(g_edit);
        ::SendMessageW(g_edit, EM_SETSEL, existing, existing);
        ::SendMessageW(g_edit, EM_REPLACESEL, FALSE,
            reinterpret_cast<LPARAM>(wline.c_str()));
    }

    // ------------------------------------------------------------
    // Public API
    // ------------------------------------------------------------

    // Must be called once when UI log EDIT is created
    export void init(HWND hostWindow, HWND editControl)
    {
        std::scoped_lock lock(g_mutex);
        g_host = hostWindow;
        g_edit = editControl;
    }

    export void log(std::string_view msg)
    {
        std::scoped_lock lock(g_mutex);

        if (!g_edit)
        {
            // Queue until the edit control is ready
            g_queue.emplace_back(msg);
            if (g_host)
            {
                ::PostMessageW(g_host, WM_LOG_FLUSH, 0, 0);
            }
            return;
        }

        append_to_edit_locked(msg);
    }

    export void log(const std::string& msg)
    {
        log(std::string_view{ msg });
    }

    export void log(const char* msg)
    {
        if (!msg) return;
        log(std::string_view{ msg });
    }

    // Called from window proc on WM_LOG_FLUSH
    export void flush_to_edit()
    {
        std::scoped_lock lock(g_mutex);
        if (!g_edit || g_queue.empty())
            return;

        for (const auto& s : g_queue)
        {
            append_to_edit_locked(s);
        }
        g_queue.clear();
    }
}
