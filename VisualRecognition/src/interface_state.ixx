module;
#define NOMINMAX
#include <windows.h>
#include "interface_ids.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>
#include <algorithm> // for std::clamp, if you use it here

export module interface.app:state;

import std;
import vision.recognition_engine;

namespace ui::detail
{
    using vision::PatchRecognizer;

    // -----------------------------------------------------------------
    // Constants
    // -----------------------------------------------------------------
    inline constexpr int  kCaptureRadius = 240;
    inline constexpr int  kDefaultPatchSize = 1;
    inline constexpr int  kDefaultBackupRetention = 5;
    inline constexpr char kModelFileName[] = "pixelai_examples.bin";

    // -----------------------------------------------------------------
    // Types
    // -----------------------------------------------------------------
    struct Capture
    {
        std::vector<std::uint32_t>           pixels;
        int                                  width{};
        int                                  height{};
        std::chrono::system_clock::time_point timestamp{};
        std::optional<std::filesystem::path>  filePath;
    };

    // -----------------------------------------------------------------
    // Global UI state  **single definitions live here**
    // -----------------------------------------------------------------
    export inline HWND g_mainWindow = nullptr;
    export inline HWND g_status = nullptr;
    export inline HWND g_preview = nullptr;
    export inline HWND g_historyLabel = nullptr;
    export inline HWND g_logEdit = nullptr;

    export inline PatchRecognizer      g_ai{};
    export inline std::vector<Capture> g_history{};
    export inline int                  g_selectedIndex = -1;
    export inline std::atomic_bool     g_isCapturing = false;

    // -----------------------------------------------------------------
    // Helper API exported for other partitions
    // -----------------------------------------------------------------
    export void SetStatus(const std::wstring& text);
    export void UpdateHistoryLabel();
    export Capture* CurrentCaptureMutable();
    export const Capture* CurrentCapture();
    export void           SelectCapture(int index);
}

// ---------------------------------------------------------------------
// Implementations
// ---------------------------------------------------------------------
namespace ui::detail
{
    export void SetStatus(const std::wstring& text)
    {
        if (g_status)
        {
            ::SetWindowTextW(g_status, text.c_str());
        }
    }

    export const Capture* CurrentCapture()
    {
        if (g_selectedIndex < 0 ||
            g_selectedIndex >= static_cast<int>(g_history.size()))
        {
            return nullptr;
        }
        return &g_history[static_cast<std::size_t>(g_selectedIndex)];
    }

    export Capture* CurrentCaptureMutable()
    {
        if (g_selectedIndex < 0 ||
            g_selectedIndex >= static_cast<int>(g_history.size()))
        {
            return nullptr;
        }
        return &g_history[static_cast<std::size_t>(g_selectedIndex)];
    }

    export void UpdateHistoryLabel()
    {
        if (!g_historyLabel)
            return;

        std::wstring text;
        if (g_history.empty())
        {
            text = L"No captures yet.";
        }
        else
        {
            text = L"Capture ";
            text += std::to_wstring(static_cast<long long>(g_selectedIndex + 1));
            text += L" / ";
            text += std::to_wstring(static_cast<long long>(g_history.size()));
        }

        ::SetWindowTextW(g_historyLabel, text.c_str());
    }

    export void SelectCapture(int index)
    {
        if (g_history.empty())
        {
            g_selectedIndex = -1;
            UpdateHistoryLabel();
            if (g_preview)
                ::InvalidateRect(g_preview, nullptr, TRUE);
            return;
        }

        index = std::clamp(
            index,
            0,
            static_cast<int>(g_history.size() - 1));

        g_selectedIndex = index;
        UpdateHistoryLabel();
        if (g_preview)
            ::InvalidateRect(g_preview, nullptr, TRUE);
    }
}
