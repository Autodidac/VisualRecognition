module;
#define NOMINMAX
#include <windows.h>
#include "interface_ids.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

export module interface.app:capture;

import std;
import vision.recognition_engine;
import :state;
import :storage;
import automation.macro.types;
import automation.macro.engine;

namespace ui::detail
{
    using vision::PatchRecognizer;

    // -----------------------------------------------------------------
    // FULL-BODY CAPTURE (mouse = head anchor)
    // -----------------------------------------------------------------
    //
    // Dimensions:
    //   width  ≈ 360 px
    //   height ≈ 960 px
    //   mouse is ~12% from the top → good head/body ratio
    //
    // This sampler ALWAYS returns the same rectangle no matter where the
    // user clicks, and will never offset the anchor when near screen edges.
    // -----------------------------------------------------------------

    constexpr int captureWidth = 360;
    constexpr int captureHeight = 960;
    constexpr int halfWidth = captureWidth / 2;
    constexpr float HEAD_POS = 0.12f;
    constexpr int headOffsetY = static_cast<int>(captureHeight * HEAD_POS);

    struct CaptureBounds
    {
        int desiredLeft;
        int desiredTop;
        int left;
        int top;
        int right;
        int bottom;

        [[nodiscard]] constexpr int width() const { return right - left + 1; }
        [[nodiscard]] constexpr int height() const { return bottom - top + 1; }
    };

    struct CursorPoint
    {
        int x;
        int y;
    };

    struct VirtualScreen
    {
        int left;
        int top;
        int right;   // inclusive
        int bottom;  // inclusive

        [[nodiscard]] constexpr int width() const { return right - left + 1; }
        [[nodiscard]] constexpr int height() const { return bottom - top + 1; }
    };

    constexpr VirtualScreen MakeVirtualScreen(int left, int top, int width, int height)
    {
        return VirtualScreen
        {
            left,
            top,
            left + width - 1,
            top + height - 1,
        };
    }

    constexpr CaptureBounds ComputeBounds(CursorPoint pt, const VirtualScreen& virtualScreen)
    {
        const int desiredLeft = pt.x - halfWidth;
        const int desiredTop = pt.y - headOffsetY;
        const int desiredRight = desiredLeft + captureWidth - 1;
        const int desiredBottom = desiredTop + captureHeight - 1;

        CaptureBounds bounds{};
        bounds.desiredLeft = desiredLeft;
        bounds.desiredTop = desiredTop;
        bounds.left = std::clamp(desiredLeft, virtualScreen.left, virtualScreen.right);
        bounds.top = std::clamp(desiredTop, virtualScreen.top, virtualScreen.bottom);
        bounds.right = std::clamp(desiredRight, virtualScreen.left, virtualScreen.right);
        bounds.bottom = std::clamp(desiredBottom, virtualScreen.top, virtualScreen.bottom);

        return bounds;
    }

    // Coverage for multi-monitor coordinates that cross the primary origin.
    static_assert(ComputeBounds({ -10, 500 }, MakeVirtualScreen(-1920, 0, 3840, 1080)).left == -190);
    static_assert(ComputeBounds({ -10, 500 }, MakeVirtualScreen(-1920, 0, 3840, 1080)).width() == captureWidth);
    static_assert(ComputeBounds({ -1910, 500 }, MakeVirtualScreen(-1920, 0, 3840, 1080)).left == -1920);

    std::optional<Capture> CapturePatchAroundCursor()
    {
        POINT pt{};
        if (!::GetCursorPos(&pt))
            return std::nullopt;

        const int virtualLeft = ::GetSystemMetrics(SM_XVIRTUALSCREEN);
        const int virtualTop = ::GetSystemMetrics(SM_YVIRTUALSCREEN);
        const int virtualWidth = ::GetSystemMetrics(SM_CXVIRTUALSCREEN);
        const int virtualHeight = ::GetSystemMetrics(SM_CYVIRTUALSCREEN);

        const VirtualScreen virtualScreen = MakeVirtualScreen(
            virtualLeft,
            virtualTop,
            virtualWidth,
            virtualHeight);

        const auto bounds = ComputeBounds({ pt.x, pt.y }, virtualScreen);
        const int w = bounds.width();
        const int h = bounds.height();

        HDC screenDC = ::GetDC(nullptr);
        HDC memDC = ::CreateCompatibleDC(screenDC);

        if (!screenDC || !memDC)
        {
            if (screenDC) ::ReleaseDC(nullptr, screenDC);
            return std::nullopt;
        }

        // Prepare DIBSection for raw pixel access
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = w;
        bi.bmiHeader.biHeight = -h;           // top-down
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;

        void* dibPixels = nullptr;
        HBITMAP hbm = ::CreateDIBSection(memDC, &bi,
            DIB_RGB_COLORS,
            &dibPixels,
            nullptr, 0);

        if (!hbm || !dibPixels)
        {
            if (hbm) ::DeleteObject(hbm);
            ::DeleteDC(memDC);
            ::ReleaseDC(nullptr, screenDC);
            return std::nullopt;
        }

        HGDIOBJ old = ::SelectObject(memDC, hbm);

        // One fast BitBlt
        ::BitBlt(memDC, 0, 0, w, h, screenDC, bounds.left, bounds.top, SRCCOPY | CAPTUREBLT);

        ::ReleaseDC(nullptr, screenDC);

        // Our final output buffer
        std::vector<std::uint32_t> outPixels(
            captureWidth * captureHeight, 0xFF000000u);

        // Base pointer to captured DIB
        const std::uint32_t* src = reinterpret_cast<const uint32_t*>(dibPixels);

        // -----------------------------
        // Remap pixels into ALWAYS FULL-SIZED portrait frame
        // -----------------------------
        for (int y = 0; y < captureHeight; ++y)
        {
            const int worldY = bounds.desiredTop + y;
            const int clampedY = std::clamp(worldY, bounds.top, bounds.bottom);
            const int sy = clampedY - bounds.top;

            for (int x = 0; x < captureWidth; ++x)
            {
                const int worldX = bounds.desiredLeft + x;
                const int clampedX = std::clamp(worldX, bounds.left, bounds.right);
                const int sx = clampedX - bounds.left;

                const uint32_t pixel = src[sy * w + sx];

                outPixels[y * captureWidth + x] = pixel | 0xFF000000u;
            }
        }

        // Cleanup
        ::SelectObject(memDC, old);
        ::DeleteObject(hbm);
        ::DeleteDC(memDC);

        // Build result Capture
        Capture cap{};
        cap.width = captureWidth;
        cap.height = captureHeight;
        cap.timestamp = std::chrono::system_clock::now();
        cap.filePath = std::nullopt;
        cap.pixels = std::move(outPixels);

        return cap;
    }


    // -----------------------------------------------------------------
    // High-level actions
    // -----------------------------------------------------------------

    static bool g_inCapture = false;

    struct CaptureGuard
    {
        bool&               reentry;
        std::atomic_bool&   hookFlag;

        CaptureGuard(bool& r, std::atomic_bool& f) : reentry(r), hookFlag(f)
        {
            reentry = true;
            hookFlag.store(true, std::memory_order_relaxed);
        }

        ~CaptureGuard()
        {
            reentry = false;
            hookFlag.store(false, std::memory_order_relaxed);
        }
    };

    export void DoCapture()
    {
        if (g_inCapture)
            return;

        CaptureGuard guard{ g_inCapture, g_isCapturing };

        auto capOpt = CapturePatchAroundCursor();
        if (!capOpt)
        {
            SetStatus(L"Capture failed.");
            return;
        }

        Capture cap = std::move(*capOpt);
        const auto saved = SaveCaptureToDisk(cap);
        cap.filePath = saved;

        g_history.push_back(std::move(cap));

        const int newIndex = static_cast<int>(g_history.size()) - 1;
        SelectCapture(newIndex);

        std::wstringstream status;
        status << L"Captured patch around cursor ("
               << g_history.size()
               << L" stored).";
        SetStatus(status.str());

        macro::g_lastTick.store(macro::nowMs());
    }

    export void DoLearnFromSelected(const std::wstring& labelW)
    {
        const auto* cap = CurrentCapture();
        if (!cap)
        {
            SetStatus(L"No capture selected for learning.");
            return;
        }
        if (labelW.empty())
        {
            SetStatus(L"Empty label; learning skipped.");
            return;
        }

        const std::string label = macro::narrow_from_wide(labelW);

        const bool ok =
            g_ai.add_example_bgra32(
                std::span<const std::uint32_t>(cap->pixels.data(), cap->pixels.size()),
                cap->width,
                cap->height,
                label);

        if (!ok)
        {
            SetStatus(L"Failed to add example to model.");
            return;
        }

        if (!SaveModel(g_ai))
            SetStatus(L"Example learned, but saving model failed.");
        else
            SetStatus(L"Learned example + saved model.");
    }

    export void DoClassify()
    {
        const auto* cap = CurrentCapture();
        if (!cap)
        {
            SetStatus(L"No capture to classify.");
            return;
        }

        float confidence = 0.0f;

        auto result = g_ai.classify_bgra32(
            std::span<const std::uint32_t>(cap->pixels.data(), cap->pixels.size()),
            cap->width,
            cap->height,
            &confidence);

        if (!result || result->empty())
        {
            SetStatus(L"Model returned no label.");
            return;
        }

        const std::string& label = *result;

        std::wstringstream ss;
        ss << L"Classified as '"
            << std::wstring(label.begin(), label.end())
            << L"' (confidence "
            << std::fixed << std::setprecision(3) << (confidence * 100.0f)
            << L"%)";

        SetStatus(ss.str());
    }

    // -----------------------------------------------------------------
    // History navigation
    // -----------------------------------------------------------------

    export void SelectPreviousCapture()
    {
        if (g_history.empty())
        {
            SetStatus(L"No captures.");
            return;
        }
        if (g_selectedIndex <= 0)
        {
            SetStatus(L"Already at oldest capture.");
            return;
        }
        SelectCapture(g_selectedIndex - 1);
        SetStatus(L"Selected previous capture.");
    }

    export void SelectNextCapture()
    {
        if (g_history.empty())
        {
            SetStatus(L"No captures.");
            return;
        }
        if (g_selectedIndex >= static_cast<int>(g_history.size()) - 1)
        {
            SetStatus(L"Already at newest capture.");
            return;
        }
        SelectCapture(g_selectedIndex + 1);
        SetStatus(L"Selected next capture.");
    }

    export void ClearHistory()
    {
        if (g_history.empty())
        {
            SetStatus(L"No captures to clear.");
            return;
        }

        std::size_t deletedFiles = 0;
        for (const auto& entry : g_history)
        {
            if (!entry.filePath)
                continue;

            std::error_code ec;
            if (std::filesystem::remove(*entry.filePath, ec))
                ++deletedFiles;
        }

        g_history.clear();
        g_selectedIndex = -1;
        UpdateHistoryLabel();

        if (g_preview)
            ::InvalidateRect(g_preview, nullptr, TRUE);

        std::wstringstream status;
        status << L"Cleared preview and history (deleted "
               << deletedFiles
               << L" capture files).";
        SetStatus(status.str());
    }

    export void DeleteSelectedCapture()
    {
        if (g_history.empty() ||
            g_selectedIndex < 0 ||
            g_selectedIndex >= static_cast<int>(g_history.size()))
        {
            SetStatus(L"No capture selected.");
            return;
        }

        const Capture& capture = g_history[static_cast<std::size_t>(g_selectedIndex)];

        std::wstring statusMessage;
        if (capture.filePath)
        {
            std::error_code ec;
            const bool removed = std::filesystem::remove(*capture.filePath, ec);

            if (ec)
            {
                statusMessage = L"Failed to delete capture file: ";
                statusMessage += capture.filePath->wstring();
            }
            else if (!removed)
            {
                statusMessage = L"Capture file missing on delete: ";
                statusMessage += capture.filePath->wstring();
            }
            else
            {
                statusMessage = L"Deleted capture file: ";
                statusMessage += capture.filePath->wstring();
            }
        }

        g_history.erase(g_history.begin() + g_selectedIndex);

        if (g_history.empty())
        {
            g_selectedIndex = -1;
            UpdateHistoryLabel();
            if (g_preview)
                ::InvalidateRect(g_preview, nullptr, TRUE);
            if (!statusMessage.empty())
                statusMessage += L" | ";
            statusMessage += L"Deleted capture; history empty.";
            SetStatus(statusMessage);
            return;
        }

        if (g_selectedIndex >= static_cast<int>(g_history.size()))
            g_selectedIndex = static_cast<int>(g_history.size()) - 1;

        SelectCapture(g_selectedIndex);
        if (!statusMessage.empty())
            statusMessage += L" | ";
        statusMessage += L"Deleted capture (";
        statusMessage += std::to_wstring(static_cast<long long>(g_history.size()));
        statusMessage += L" remaining).";
        SetStatus(statusMessage);
    }

} // namespace ui::detail
