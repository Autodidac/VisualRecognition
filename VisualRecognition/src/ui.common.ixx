module;
#define NOMINMAX
#include <windows.h>
#include "ids.hpp"
#include <string>
#include <print>
#include <vector>
#include <optional>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <utility>

export module ui:common;

import std;
import pixelai;

namespace ui::detail
{
    // Shared constants/state
    constexpr int  kCaptureRadius = 240;
    constexpr char kModelFile[] = "pixelai_examples.bin";
    constexpr int  kDefaultBackupRetention = 5;
    constexpr int  kDefaultPatchSize = 1;

    HWND g_status = nullptr;
    HWND g_preview = nullptr;
    HWND g_historyLabel = nullptr;
    HWND g_mainWindow = nullptr;
    HHOOK g_mouseHook = nullptr;
    HHOOK g_keyboardHook = nullptr;
    bool g_mouseHookPaused = false;

    almond::pixelai::PixelRecognizer g_ai{};

    struct Capture
    {
        std::vector<std::uint32_t> pixels;
        int width{};
        int height{};
        std::chrono::system_clock::time_point timestamp{};
        std::optional<std::filesystem::path> filePath;
    };

    std::vector<Capture> g_history;
    int g_selectedIndex = -1;

    // Text helpers
    inline std::string narrow_utf8(std::wstring_view wstr)
    {
        if (wstr.empty())
            return {};

        int needed = ::WideCharToMultiByte(CP_UTF8, 0,
            wstr.data(), static_cast<int>(wstr.size()),
            nullptr, 0, nullptr, nullptr);
        if (needed <= 0)
            return {};

        std::string result(static_cast<std::size_t>(needed), '\0');
        ::WideCharToMultiByte(CP_UTF8, 0,
            wstr.data(), static_cast<int>(wstr.size()),
            result.data(), needed,
            nullptr, nullptr);
        return result;
    }

    // Status helper
    inline void SetStatus(std::wstring_view text)
    {
        if (g_status)
            ::SetWindowTextW(g_status, text.data());
    }

    inline std::wstring FormatBackupPath(const std::filesystem::path& path)
    {
        std::error_code ec{};
        auto absolutePath = std::filesystem::absolute(path, ec);
        const auto& displayPath = ec ? path : absolutePath;
        return displayPath.wstring();
    }

    inline const Capture* CurrentCapture()
    {
        if (g_selectedIndex < 0 || g_selectedIndex >= static_cast<int>(g_history.size()))
            return nullptr;
        return &g_history[static_cast<std::size_t>(g_selectedIndex)];
    }

    inline std::wstring FormatTimestamp(const std::chrono::system_clock::time_point& tp)
    {
        auto t = std::chrono::system_clock::to_time_t(tp);
        tm localTm{};
        localtime_s(&localTm, &t);

        wchar_t buffer[64]{};
        std::wcsftime(buffer, std::size(buffer), L"%Y-%m-%d %H:%M:%S", &localTm);
        return buffer;
    }

    inline void UpdateHistoryLabel()
    {
        if (!g_historyLabel)
            return;

        if (g_history.empty() || !CurrentCapture())
        {
            ::SetWindowTextW(g_historyLabel, L"No capture selected.");
            return;
        }

        const auto& cap = *CurrentCapture();
        std::wstring text = L"Capture "
            + std::to_wstring(static_cast<unsigned long long>(g_selectedIndex + 1))
            + L"/" + std::to_wstring(static_cast<unsigned long long>(g_history.size()))
            + L" — " + FormatTimestamp(cap.timestamp);

        ::SetWindowTextW(g_historyLabel, text.c_str());
    }

    inline void SelectCapture(int index)
    {
        if (index < 0 || index >= static_cast<int>(g_history.size()))
            return;
        g_selectedIndex = index;
        UpdateHistoryLabel();
        if (g_preview)
            ::InvalidateRect(g_preview, nullptr, TRUE);
    }
}
