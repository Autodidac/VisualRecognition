module;
#define NOMINMAX
#include <windows.h>

#include "ids.hpp"

#include <chrono>
#include <vector>
#include <optional>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <filesystem>
#include <sstream>
#include <iomanip>

export module ui:capture;

import std;
import pixelai;
import :common;
import :filesystem;

namespace ui::detail
{
    // Real screen capture around cursor (multi-monitor safe)
    std::optional<Capture> CapturePatchAroundCursor()
    {
        POINT pt{};
        if (!::GetCursorPos(&pt))
            return std::nullopt;

        const LONG vx = ::GetSystemMetrics(SM_XVIRTUALSCREEN);
        const LONG vy = ::GetSystemMetrics(SM_YVIRTUALSCREEN);
        const LONG vw = ::GetSystemMetrics(SM_CXVIRTUALSCREEN);
        const LONG vh = ::GetSystemMetrics(SM_CYVIRTUALSCREEN);

        const LONG left = vx;
        const LONG top = vy;
        const LONG right = vx + vw - 1;
        const LONG bottom = vy + vh - 1;

        const LONG r = static_cast<LONG>(kCaptureRadius);
        const int  targetW = static_cast<int>(r * 2 + 1);
        const int  targetH = static_cast<int>(r * 2 + 1);

        const LONG desiredX0 = pt.x - r;
        const LONG desiredY0 = pt.y - r;
        const LONG desiredX1 = pt.x + r;
        const LONG desiredY1 = pt.y + r;

        LONG x0 = std::clamp(desiredX0, left, right);
        LONG y0 = std::clamp(desiredY0, top, bottom);
        LONG x1 = std::clamp(desiredX1, left, right);
        LONG y1 = std::clamp(desiredY1, top, bottom);

        int w = static_cast<int>(x1 - x0 + 1);
        int h = static_cast<int>(y1 - y0 + 1);

        if (w <= 0 || h <= 0)
            return std::nullopt;

        int offsetX = static_cast<int>(x0 - desiredX0);
        int offsetY = static_cast<int>(y0 - desiredY0);

        HDC hdcScreen = ::GetDC(nullptr);
        if (!hdcScreen)
            return std::nullopt;

        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = w;
        bmi.bmiHeader.biHeight = -h; // top-down
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        void* bits = nullptr;
        HBITMAP hbm = ::CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!hbm || !bits)
        {
            if (hbm) ::DeleteObject(hbm);
            ::ReleaseDC(nullptr, hdcScreen);
            return std::nullopt;
        }

        HDC hdcMem = ::CreateCompatibleDC(hdcScreen);
        HGDIOBJ old = ::SelectObject(hdcMem, hbm);

        ::BitBlt(hdcMem,
            0, 0, w, h,
            hdcScreen,
            static_cast<int>(x0),
            static_cast<int>(y0),
            SRCCOPY);

        ::SelectObject(hdcMem, old);
        ::DeleteDC(hdcMem);
        ::ReleaseDC(nullptr, hdcScreen);

        std::vector<std::uint32_t> captured;
        captured.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
        std::memcpy(captured.data(), bits, captured.size() * sizeof(std::uint32_t));

        ::DeleteObject(hbm);

        std::vector<std::uint32_t> padded;
        padded.resize(static_cast<std::size_t>(targetW) * static_cast<std::size_t>(targetH), 0u);

        for (int row = 0; row < h; ++row)
        {
            const auto* src = captured.data() + static_cast<std::size_t>(row) * static_cast<std::size_t>(w);
            auto* dst = padded.data() + static_cast<std::size_t>(row + offsetY) * static_cast<std::size_t>(targetW)
                + static_cast<std::size_t>(offsetX);
            std::memcpy(dst, src, static_cast<std::size_t>(w) * sizeof(std::uint32_t));
        }

        Capture cap{
            .pixels = std::move(padded),
            .width = targetW,
            .height = targetH,
            .timestamp = std::chrono::system_clock::now()
        };

        return cap;
    }

    // Capture + classify logic
    void DoCapture()
    {
        auto cap = CapturePatchAroundCursor();
        if (cap)
        {
            g_history.push_back(std::move(*cap));
            g_selectedIndex = static_cast<int>(g_history.size() - 1);
            auto savedPath = SaveCaptureToDisk(g_history.back());
            g_history.back().filePath = savedPath;
            UpdateHistoryLabel();
            if (savedPath)
                SetStatus(L"Captured patch (click).");
            else
                SetStatus(L"Captured, but failed to save history entry.");
            if (g_preview)
                ::InvalidateRect(g_preview, nullptr, TRUE);
        }
        else
        {
            SetStatus(L"Capture failed.");
        }
    }

    void DoClassify()
    {
        const auto* cap = CurrentCapture();
        if (!cap)
        {
            SetStatus(L"No capture to classify.");
            return;
        }

        float score = 0.0f;
        auto  labelOpt = g_ai.classify_bgra32(cap->pixels, cap->width, cap->height, &score);

        if (labelOpt)
        {
            std::wstring msg = L"Classified: ";
            msg += std::wstring(labelOpt->begin(), labelOpt->end());
            msg += L" (score ";

            std::wostringstream oss;
            oss << std::fixed << std::setprecision(3) << score;
            msg += oss.str();
            msg += L")";
            SetStatus(msg);
        }
        else
        {
            SetStatus(L"Unknown region.");
        }

        if (g_preview)
            ::InvalidateRect(g_preview, nullptr, TRUE);
    }

    void SelectPreviousCapture()
    {
        if (g_history.empty())
        {
            SetStatus(L"No capture history available.");
            return;
        }

        int target = std::max(g_selectedIndex - 1, 0);
        if (target == g_selectedIndex)
        {
            SetStatus(L"Already at the oldest capture.");
            return;
        }

        SelectCapture(target);
        SetStatus(L"Selected previous capture.");
    }

    void SelectNextCapture()
    {
        if (g_history.empty())
        {
            SetStatus(L"No capture history available.");
            return;
        }

        int target = std::min(g_selectedIndex + 1, static_cast<int>(g_history.size() - 1));
        if (target == g_selectedIndex)
        {
            SetStatus(L"Already at the newest capture.");
            return;
        }

        SelectCapture(target);
        SetStatus(L"Selected next capture.");
    }

    void DeleteSelectedCapture()
    {
        if (g_history.empty() || g_selectedIndex < 0 || g_selectedIndex >= static_cast<int>(g_history.size()))
        {
            SetStatus(L"No capture selected to delete.");
            return;
        }

        auto removedIndex = g_selectedIndex;
        auto removed = std::move(g_history[static_cast<std::size_t>(removedIndex)]);

        bool removedFromDisk = true;
        if (removed.filePath)
        {
            std::error_code ec{};
            removedFromDisk = std::filesystem::remove(*removed.filePath, ec) || !std::filesystem::exists(*removed.filePath);
        }

        g_history.erase(g_history.begin() + removedIndex);

        if (g_history.empty())
        {
            g_selectedIndex = -1;
            UpdateHistoryLabel();
            if (g_preview)
                ::InvalidateRect(g_preview, nullptr, TRUE);

            SetStatus(removedFromDisk ? L"Deleted capture; history is now empty." : L"Deleted capture, but failed to remove file.");
            return;
        }

        g_selectedIndex = std::min(removedIndex, static_cast<int>(g_history.size() - 1));
        UpdateHistoryLabel();
        if (g_preview)
            ::InvalidateRect(g_preview, nullptr, TRUE);

        SetStatus(removedFromDisk ? L"Deleted capture." : L"Deleted capture, but failed to remove file from disk.");
    }
}
