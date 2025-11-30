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
    // Capture padded 481×481 BGRA patch around cursor
    // -----------------------------------------------------------------

    std::optional<Capture> CapturePatchAroundCursor()
    {
        POINT pt{};
        if (!::GetCursorPos(&pt))
            return std::nullopt;

        HDC screenDC = ::GetDC(nullptr);
        if (!screenDC)
            return std::nullopt;

        const int screenW = ::GetSystemMetrics(SM_CXSCREEN);
        const int screenH = ::GetSystemMetrics(SM_CYSCREEN);

        const int radius = kCaptureRadius;
        const int patchW = radius * 2 + 1;
        const int patchH = radius * 2 + 1;

        const int x0 = std::clamp(static_cast<int>(pt.x - radius), 0, screenW - 1);
        const int y0 = std::clamp(static_cast<int>(pt.y - radius), 0, screenH - 1);
        const int x1 = std::clamp(static_cast<int>(pt.x + radius), 0, screenW - 1);
        const int y1 = std::clamp(static_cast<int>(pt.y + radius), 0, screenH - 1);

        const int w = x1 - x0 + 1;
        const int h = y1 - y0 + 1;
        if (w <= 0 || h <= 0)
        {
            ::ReleaseDC(nullptr, screenDC);
            return std::nullopt;
        }

        HDC memDC = ::CreateCompatibleDC(screenDC);
        if (!memDC)
        {
            ::ReleaseDC(nullptr, screenDC);
            return std::nullopt;
        }

        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = w;
        bi.bmiHeader.biHeight = -h; // top-down
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;

        void* bits = nullptr;
        HBITMAP bmp = ::CreateDIBSection(screenDC, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!bmp)
        {
            ::DeleteDC(memDC);
            ::ReleaseDC(nullptr, screenDC);
            return std::nullopt;
        }

        HGDIOBJ oldBmp = ::SelectObject(memDC, bmp);

        if (!::BitBlt(memDC,
            0, 0, w, h,
            screenDC,
            x0, y0,
            SRCCOPY))
        {
            ::SelectObject(memDC, oldBmp);
            ::DeleteObject(bmp);
            ::DeleteDC(memDC);
            ::ReleaseDC(nullptr, screenDC);
            return std::nullopt;
        }

        std::vector<std::uint32_t> captured(static_cast<std::size_t>(w) * h);
        std::memcpy(captured.data(), bits, captured.size() * sizeof(std::uint32_t));

        ::SelectObject(memDC, oldBmp);
        ::DeleteObject(bmp);
        ::DeleteDC(memDC);
        ::ReleaseDC(nullptr, screenDC);

        // Pad to exact (2r+1)×(2r+1)
        const int targetW = patchW;
        const int targetH = patchH;

        std::vector<std::uint32_t> padded(static_cast<std::size_t>(targetW) * targetH, 0);

        const int offsetX = (targetW - w) / 2;
        const int offsetY = (targetH - h) / 2;

        for (int row = 0; row < h; ++row)
        {
            const auto* src = captured.data() + static_cast<std::size_t>(row) * w;
            auto* dst = padded.data() +
                static_cast<std::size_t>(row + offsetY) * targetW +
                offsetX;

            std::memcpy(dst, src, sizeof(std::uint32_t) * w);
        }

        Capture out{};
        out.pixels.assign(padded.begin(), padded.end());
        out.width = targetW;
        out.height = targetH;
        out.timestamp = std::chrono::system_clock::now();
        out.filePath = std::nullopt;
        return out;
    }

    // -----------------------------------------------------------------
    // High-level actions
    // -----------------------------------------------------------------

    export void DoCapture()
    {
        auto capOpt = CapturePatchAroundCursor();
        if (!capOpt)
        {
            SetStatus(L"Capture failed.");
            return;
        }

        Capture cap;
        cap = std::move(*capOpt);
        const auto saved = SaveCaptureToDisk(cap);
        cap.filePath = saved;

        g_history.push_back(std::move(cap));
        g_selectedIndex = static_cast<int>(g_history.size()) - 1;

        UpdateHistoryLabel();
        SetStatus(L"Captured patch around cursor.");

        // Reset macro timing when we capture; this keeps subsequent events sane.
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

        // SAFE conversion: UTF-16 → UTF-8
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
        {
            SetStatus(L"Example learned, but saving model failed.");
        }
        else
        {
            SetStatus(L"Learned example + saved model.");
        }
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

        // PixelAI returns optional<string>
        auto result = g_ai.classify_bgra32(
            std::span<const std::uint32_t>(cap->pixels.data(), cap->pixels.size()),
            cap->width,
            cap->height,
            &confidence     // PASS POINTER HERE
        );

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

        // Optional: only if you want future automation.
        // Remove if not implemented.
        // macro::on_ai_classification(label, confidence);
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

        const int target = std::max(g_selectedIndex - 1, 0);
        if (target == g_selectedIndex)
        {
            SetStatus(L"Already at oldest capture.");
            return;
        }

        SelectCapture(target);
        SetStatus(L"Selected previous capture.");
    }

    export void SelectNextCapture()
    {
        if (g_history.empty())
        {
            SetStatus(L"No captures.");
            return;
        }

        if (g_selectedIndex < 0 ||
            g_selectedIndex >= static_cast<int>(g_history.size()) - 1)
        {
            SetStatus(L"Already at newest capture.");
            return;
        }

        const int target = std::min(
            g_selectedIndex + 1,
            static_cast<int>(g_history.size()) - 1);

        if (target == g_selectedIndex)
        {
            SetStatus(L"Already at newest capture.");
            return;
        }

        SelectCapture(target);
        SetStatus(L"Selected next capture.");
    }

    export void DeleteSelectedCapture()
    {
        if (g_history.empty() || g_selectedIndex < 0 ||
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
            SetStatus(L"Deleted capture; history empty.");
            return;
        }

        if (g_selectedIndex >= static_cast<int>(g_history.size()))
            g_selectedIndex = static_cast<int>(g_history.size()) - 1;

        SelectCapture(g_selectedIndex);
        SetStatus(L"Deleted capture.");
    }

} // namespace ui::detail
