module;
#define NOMINMAX
#include <windows.h>
#include "ids.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

export module ui:capture;

import std;
import pixelai;
import :common;
import :filesystem;
import vr.macro.types;
import vr.macro.core;

namespace ui::detail
{
    using pixelai::PixelRecognizer;

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

    std::optional<Capture> CapturePatchAroundCursor()
    {
        POINT pt{};
        if (!::GetCursorPos(&pt))
            return std::nullopt;

        const int screenW = ::GetSystemMetrics(SM_CXSCREEN);
        const int screenH = ::GetSystemMetrics(SM_CYSCREEN);

        // -----------------------------
        // Full-body rectangle parameters
        // -----------------------------
        constexpr int captureWidth = 360;
        constexpr int captureHeight = 960;
        constexpr int halfWidth = captureWidth / 2;
        constexpr float HEAD_POS = 0.12f;
        constexpr int headOffsetY = static_cast<int>(captureHeight * HEAD_POS);

        // -----------------------------
        // Determine screen rect to capture once
        // -----------------------------
        // We capture the bounding rectangle around the full-body area,
        // clamped within the screen.
        int left = pt.x - halfWidth;
        int top = pt.y - headOffsetY;
        int right = left + captureWidth - 1;
        int bottom = top + captureHeight - 1;

        // Clamp bounding box
        left = std::clamp(left, 0, screenW - 1);
        top = std::clamp(top, 0, screenH - 1);
        right = std::clamp(right, 0, screenW - 1);
        bottom = std::clamp(bottom, 0, screenH - 1);

        const int w = right - left + 1;
        const int h = bottom - top + 1;

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
        ::BitBlt(memDC, 0, 0, w, h, screenDC, left, top, SRCCOPY | CAPTUREBLT);

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
            int sy = y; // y already aligned due to top/bottom bounds
            if (sy >= h) sy = h - 1;

            for (int x = 0; x < captureWidth; ++x)
            {
                int sx = x;
                if (sx >= w) sx = w - 1;

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

    export void DoCapture()
    {
        if (g_inCapture)
            return;

        struct Guard { bool& f; ~Guard() { f = false; } };
        g_inCapture = true;
        Guard guard{ g_inCapture };

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

        SetStatus(L"Captured patch around cursor.");

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
            << static_cast<int>(confidence * 100.0f)
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

    export void DeleteSelectedCapture()
    {
        if (g_history.empty() ||
            g_selectedIndex < 0 ||
            g_selectedIndex >= static_cast<int>(g_history.size()))
        {
            SetStatus(L"No capture selected.");
            return;
        }

        g_history.erase(g_history.begin() + g_selectedIndex);

        if (g_history.empty())
        {
            g_selectedIndex = -1;
            UpdateHistoryLabel();
            if (g_preview)
                ::InvalidateRect(g_preview, nullptr, TRUE);
            SetStatus(L"Deleted capture; history empty.");
            return;
        }

        if (g_selectedIndex >= static_cast<int>(g_history.size()))
            g_selectedIndex = static_cast<int>(g_history.size()) - 1;

        SelectCapture(g_selectedIndex);
        SetStatus(L"Deleted capture.");
    }

} // namespace ui::detail
