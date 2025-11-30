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

export module ui;

import std;
import pixelai;

using almond::pixelai::PixelRecognizer;

namespace
{
    constexpr int  kCaptureRadius = 240;
    constexpr char kModelFile[] = "pixelai_examples.bin";

    HWND g_status = nullptr;
    HWND g_preview = nullptr;
    HHOOK g_mouseHook = nullptr;
    HHOOK g_keyboardHook = nullptr;

    PixelRecognizer g_ai{};

    struct Capture
    {
        std::vector<std::uint32_t> pixels;
        int width{};
        int height{};
    };

    std::optional<Capture> g_lastCapture;

    // -------------------------------------------------------------------------
    // UTF-8 conversion
    // -------------------------------------------------------------------------
    std::string narrow_utf8(std::wstring_view wstr)
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

    // -------------------------------------------------------------------------
    // Status helper
    // -------------------------------------------------------------------------
    inline void SetStatus(std::wstring_view text)
    {
        if (g_status)
            ::SetWindowTextW(g_status, text.data());
    }

    // -------------------------------------------------------------------------
    // Real screen capture around cursor into g_lastCapture (multi-monitor safe)
    // -------------------------------------------------------------------------
    bool CapturePatchAroundCursor()
    {
        POINT pt{};
        if (!::GetCursorPos(&pt))
            return false;

        const LONG vx = ::GetSystemMetrics(SM_XVIRTUALSCREEN);
        const LONG vy = ::GetSystemMetrics(SM_YVIRTUALSCREEN);
        const LONG vw = ::GetSystemMetrics(SM_CXVIRTUALSCREEN);
        const LONG vh = ::GetSystemMetrics(SM_CYVIRTUALSCREEN);

        const LONG left = vx;
        const LONG top = vy;
        const LONG right = vx + vw - 1;
        const LONG bottom = vy + vh - 1;

        const LONG r = static_cast<LONG>(kCaptureRadius);

        LONG x0 = std::clamp(pt.x - r, left, right);
        LONG y0 = std::clamp(pt.y - r, top, bottom);
        LONG x1 = std::clamp(pt.x + r, left, right);
        LONG y1 = std::clamp(pt.y + r, top, bottom);

        int w = static_cast<int>(x1 - x0 + 1);
        int h = static_cast<int>(y1 - y0 + 1);

        if (w <= 0 || h <= 0)
            return false;

        HDC hdcScreen = ::GetDC(nullptr);
        if (!hdcScreen)
            return false;

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
            return false;
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

        std::vector<std::uint32_t> pixels;
        pixels.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
        std::memcpy(pixels.data(), bits, pixels.size() * sizeof(std::uint32_t));

        ::DeleteObject(hbm);

        g_lastCapture = Capture{
            .pixels = std::move(pixels),
            .width = w,
            .height = h
        };

        return true;
    }

    // Forward for hook
    void DoCapture();
    void DoClassify();

    // -------------------------------------------------------------------------
    // GLOBAL MOUSE HOOK — captures anywhere even when app is not focused
    // -------------------------------------------------------------------------
    LRESULT CALLBACK MouseHookProc(int code, WPARAM wParam, LPARAM lParam)
    {
        if (code >= 0)
        {
            switch (wParam)
            {
            case WM_LBUTTONDOWN:
                DoCapture();
                break;
            }
        }

        return ::CallNextHookEx(g_mouseHook, code, wParam, lParam);
    }

    // -------------------------------------------------------------------------
    // GLOBAL KEYBOARD HOOK — F5 triggers classify even when unfocused
    // -------------------------------------------------------------------------
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

    // -------------------------------------------------------------------------
    // Label entry mini-dialog
    // -------------------------------------------------------------------------
    struct PromptState
    {
        HWND hwnd{};
        HWND edit{};
        std::wstring result;
        bool done{ false };
        bool accepted{ false };
    };

    PromptState g_prompt;

    LRESULT CALLBACK PromptWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg)
        {
        case WM_CREATE:
        {
            g_prompt.hwnd = hwnd;

            RECT rc{};
            ::GetClientRect(hwnd, &rc);
            int W = rc.right - rc.left;
            int H = rc.bottom - rc.top;

            const int margin = 10;
            const int btnW = 80;
            const int btnH = 26;

            HWND hStatic = ::CreateWindowW(L"STATIC", L"Enter label:",
                WS_VISIBLE | WS_CHILD,
                margin, margin,
                W - margin * 2, 18,
                hwnd, nullptr, nullptr, nullptr);
            (void)hStatic;

            g_prompt.edit = ::CreateWindowW(L"EDIT", L"",
                WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
                margin, margin + 22,
                W - margin * 2, 24,
                hwnd, nullptr, nullptr, nullptr);

            int yBtn = H - margin - btnH;
            int xOk = W - margin - btnW * 2 - 6;
            int xCancel = W - margin - btnW;

            ::CreateWindowW(L"BUTTON", L"OK",
                WS_VISIBLE | WS_CHILD,
                xOk, yBtn, btnW, btnH,
                hwnd, (HMENU)IDOK, nullptr, nullptr);

            ::CreateWindowW(L"BUTTON", L"Cancel",
                WS_VISIBLE | WS_CHILD,
                xCancel, yBtn, btnW, btnH,
                hwnd, (HMENU)IDCANCEL, nullptr, nullptr);

            ::SetFocus(g_prompt.edit);
            break;
        }

        case WM_COMMAND:
        {
            switch (LOWORD(wParam))
            {
            case IDOK:
            {
                wchar_t buf[256]{};
                ::GetWindowTextW(
                    g_prompt.edit,
                    buf,
                    static_cast<int>(sizeof(buf) / sizeof(buf[0]))
                );
                g_prompt.result = buf;
                g_prompt.accepted = !g_prompt.result.empty();
                g_prompt.done = true;
                ::DestroyWindow(hwnd);
                return 0;
            }
            case IDCANCEL:
            {
                g_prompt.accepted = false;
                g_prompt.done = true;
                ::DestroyWindow(hwnd);
                return 0;
            }
            }
            break;
        }

        case WM_CLOSE:
            g_prompt.accepted = false;
            g_prompt.done = true;
            ::DestroyWindow(hwnd);
            return 0;
        }

        return ::DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    std::wstring PromptLabel(HWND owner)
    {
        static bool classRegistered = false;
        if (!classRegistered)
        {
            WNDCLASSW wc{};
            wc.lpfnWndProc = PromptWndProc;
            wc.hInstance = ::GetModuleHandleW(nullptr);
            wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
            wc.lpszClassName = L"AIPixelLabelPrompt";

            ::RegisterClassW(&wc);
            classRegistered = true;
        }

        g_prompt = PromptState{};

        const int W = 320;
        const int H = 140;
        RECT rcOwner{};

        if (owner && ::GetWindowRect(owner, &rcOwner))
        {
            int x = rcOwner.left + ((rcOwner.right - rcOwner.left) - W) / 2;
            int y = rcOwner.top + ((rcOwner.bottom - rcOwner.top) - H) / 2;

            ::CreateWindowExW(
                WS_EX_DLGMODALFRAME,
                L"AIPixelLabelPrompt", L"New Label",
                WS_CAPTION | WS_SYSMENU | WS_POPUPWINDOW,
                x, y, W, H,
                owner, nullptr, ::GetModuleHandleW(nullptr), nullptr
            );
        }
        else
        {
            ::CreateWindowExW(
                WS_EX_DLGMODALFRAME,
                L"AIPixelLabelPrompt", L"New Label",
                WS_CAPTION | WS_SYSMENU | WS_POPUPWINDOW,
                CW_USEDEFAULT, CW_USEDEFAULT, W, H,
                owner, nullptr, ::GetModuleHandleW(nullptr), nullptr
            );
        }

        ::ShowWindow(g_prompt.hwnd, SW_SHOW);
        ::UpdateWindow(g_prompt.hwnd);

        MSG msg{};
        while (!g_prompt.done && ::GetMessageW(&msg, nullptr, 0, 0))
        {
            if (!::IsDialogMessageW(g_prompt.hwnd, &msg))
            {
                ::TranslateMessage(&msg);
                ::DispatchMessageW(&msg);
            }
        }

        if (g_prompt.accepted)
            return g_prompt.result;
        return {};
    }

    // -------------------------------------------------------------------------
    // Layout
    // -------------------------------------------------------------------------
    inline void LayoutControls(HWND hwnd)
    {
        RECT rc{};
        ::GetClientRect(hwnd, &rc);

        int W = rc.right - rc.left;
        int H = rc.bottom - rc.top;

        const int margin = 14;
        const int spacing = 10;
        const int btnW = 150;
        const int btnH = 42;
        const int statusH = 38;

        int x = margin;
        int y = margin;

        ::MoveWindow(::GetDlgItem(hwnd, IDC_BTN_CAPTURE), x, y, btnW, btnH, TRUE);
        x += btnW + spacing;

        ::MoveWindow(::GetDlgItem(hwnd, IDC_BTN_LEARN), x, y, btnW, btnH, TRUE);
        x += btnW + spacing;

        ::MoveWindow(::GetDlgItem(hwnd, IDC_BTN_CLASSIFY), x, y, btnW, btnH, TRUE);
        x += btnW + spacing;

        ::MoveWindow(::GetDlgItem(hwnd, IDC_BTN_EXIT), x, y, btnW, btnH, TRUE);

        int previewY = y + btnH + spacing;

        ::MoveWindow(g_preview,
            margin,
            previewY,
            W - margin * 2,
            H - previewY - statusH - margin,
            TRUE);

        ::MoveWindow(g_status,
            margin,
            H - statusH - margin,
            W - margin * 2,
            statusH,
            TRUE);
    }

    // -------------------------------------------------------------------------
    // Owner-draw preview
    // -------------------------------------------------------------------------
    void DrawPreview(const DRAWITEMSTRUCT& dis)
    {
        HDC  hdc = dis.hDC;
        RECT rc = dis.rcItem;

        ::FillRect(hdc, &rc, reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1));

        if (!g_lastCapture)
            return;

        const Capture& cap = *g_lastCapture;

        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = cap.width;
        bmi.bmiHeader.biHeight = -cap.height;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        int rcW = rc.right - rc.left;
        int rcH = rc.bottom - rc.top;

        if (rcW <= 0 || rcH <= 0)
            return;

        double scaleX = static_cast<double>(rcW) / static_cast<double>(cap.width);
        double scaleY = static_cast<double>(rcH) / static_cast<double>(cap.height);
        double scale = std::min(scaleX, scaleY);

        int drawW = static_cast<int>(cap.width * scale);
        int drawH = static_cast<int>(cap.height * scale);

        int dstX = rc.left + (rcW - drawW) / 2;
        int dstY = rc.top + (rcH - drawH) / 2;

        ::StretchDIBits(hdc,
            dstX, dstY, drawW, drawH,
            0, 0, cap.width, cap.height,
            cap.pixels.data(),
            &bmi,
            DIB_RGB_COLORS,
            SRCCOPY);

        HPEN    pen = ::CreatePen(PS_SOLID, 2, RGB(255, 0, 0));
        HGDIOBJ oldPen = ::SelectObject(hdc, pen);
        HGDIOBJ oldBrush = ::SelectObject(hdc, ::GetStockObject(HOLLOW_BRUSH));

        ::Rectangle(hdc, dstX, dstY, dstX + drawW, dstY + drawH);

        ::SelectObject(hdc, oldBrush);
        ::SelectObject(hdc, oldPen);
        ::DeleteObject(pen);
    }

    // -------------------------------------------------------------------------
    // Capture + classify logic (buttons + global hook)
    // -------------------------------------------------------------------------
    void DoCapture()
    {
        if (CapturePatchAroundCursor())
        {
            SetStatus(L"Captured patch (click).");
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
        if (!g_lastCapture)
        {
            SetStatus(L"No capture to classify.");
            return;
        }

        auto& cap = *g_lastCapture;

        float score = 0.0f;
        auto  labelOpt = g_ai.classify_bgra32(cap.pixels, cap.width, cap.height, &score);

        if (labelOpt)
        {
            std::wstring msg = L"Classified: ";
            msg += std::wstring(labelOpt->begin(), labelOpt->end());
            msg += L" (score ";
            msg += std::to_wstring(score);
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

    // -------------------------------------------------------------------------
    // Main window proc
    // -------------------------------------------------------------------------
    LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg)
        {
        case WM_CREATE:
        {
            ::CreateWindowW(L"BUTTON", L"Capture Patch",
                WS_VISIBLE | WS_CHILD,
                0, 0, 0, 0,
                hwnd, (HMENU)IDC_BTN_CAPTURE,
                nullptr, nullptr);

            ::CreateWindowW(L"BUTTON", L"Learn Label",
                WS_VISIBLE | WS_CHILD,
                0, 0, 0, 0,
                hwnd, (HMENU)IDC_BTN_LEARN,
                nullptr, nullptr);

            ::CreateWindowW(L"BUTTON", L"Classify",
                WS_VISIBLE | WS_CHILD,
                0, 0, 0, 0,
                hwnd, (HMENU)IDC_BTN_CLASSIFY,
                nullptr, nullptr);

            ::CreateWindowW(L"BUTTON", L"Exit",
                WS_VISIBLE | WS_CHILD,
                0, 0, 0, 0,
                hwnd, (HMENU)IDC_BTN_EXIT,
                nullptr, nullptr);

            g_preview = ::CreateWindowW(L"STATIC", L"",
                WS_VISIBLE | WS_CHILD | SS_OWNERDRAW,
                0, 0, 0, 0,
                hwnd, (HMENU)IDC_PREVIEW,
                nullptr, nullptr);

            g_status = ::CreateWindowW(L"STATIC", L"Ready",
                WS_VISIBLE | WS_CHILD | SS_LEFT,
                0, 0, 0, 0,
                hwnd, (HMENU)IDC_STATUS,
                nullptr, nullptr);

            if (!g_ai.load_from_file(kModelFile))
            {
                SetStatus(L"Failed to load model.");
                ::MessageBoxW(hwnd,
                    L"Could not load the model file. Classification will be unavailable.",
                    L"Model Load Failed",
                    MB_OK | MB_ICONERROR);
            }

            // Global low-level mouse hook
            g_mouseHook = ::SetWindowsHookExW(
                WH_MOUSE_LL,
                MouseHookProc,
                ::GetModuleHandleW(nullptr),
                0
            );

            if (!g_mouseHook)
            {
                SetStatus(L"Global mouse hook inactive (capture unavailable).");
                ::MessageBoxW(hwnd,
                    L"Global mouse hook could not be installed. Capture via mouse clicks will not work.",
                    L"Hook Installation Failed",
                    MB_OK | MB_ICONERROR);
            }

            // Global low-level keyboard hook for F5 classification
            g_keyboardHook = ::SetWindowsHookExW(
                WH_KEYBOARD_LL,
                KeyboardHookProc,
                ::GetModuleHandleW(nullptr),
                0
            );

            if (!g_keyboardHook)
            {
                SetStatus(L"Global keyboard hook inactive (F5 classify unavailable).");
                ::MessageBoxW(hwnd,
                    L"Global keyboard hook could not be installed. Use the Classify button instead.",
                    L"Hook Installation Failed",
                    MB_OK | MB_ICONERROR);
            }

            break;
        }

        case WM_SIZE:
            LayoutControls(hwnd);
            break;

        case WM_DRAWITEM:
        {
            auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
            if (dis && dis->CtlID == IDC_PREVIEW)
            {
                DrawPreview(*dis);
                return TRUE;
            }
            break;
        }

        case WM_COMMAND:
        {
            switch (LOWORD(wParam))
            {
            case IDC_BTN_CAPTURE:
                DoCapture();
                break;

            case IDC_BTN_CLASSIFY:
                DoClassify();
                break;

            case IDC_BTN_LEARN:
            {
                if (!g_lastCapture)
                {
                    SetStatus(L"No capture to learn from.");
                    break;
                }

                std::wstring labelW = PromptLabel(hwnd);
                if (labelW.empty())
                {
                    SetStatus(L"Label entry cancelled.");
                    break;
                }

                auto& cap = *g_lastCapture;
                std::string label = narrow_utf8(labelW);

                if (g_ai.add_example_bgra32(cap.pixels, cap.width, cap.height, label))
                {
                    if (g_ai.save_to_file(kModelFile))
                    {
                        SetStatus(L"Example learned + saved.");
                    }
                    else
                    {
                        SetStatus(L"Learned, but failed to save model file.");
                    }
                }
                else
                {
                    SetStatus(L"Training failed (size mismatch?).");
                }

                break;
            }

            case IDC_BTN_EXIT:
                ::DestroyWindow(hwnd);
                break;
            }
            break;
        }

        case WM_DESTROY:
            if (g_mouseHook)
            {
                ::UnhookWindowsHookEx(g_mouseHook);
                g_mouseHook = nullptr;
            }
            if (g_keyboardHook)
            {
                ::UnhookWindowsHookEx(g_keyboardHook);
                g_keyboardHook = nullptr;
            }
            ::PostQuitMessage(0);
            return 0;
        }

        return ::DefWindowProcW(hwnd, msg, wParam, lParam);
    }

} // anonymous namespace

// -----------------------------------------------------------------------------
// Exported entrypoint used by main.cpp
// -----------------------------------------------------------------------------
export void RunUI(HINSTANCE hInstance, int cmdShow)
{
    const wchar_t CLASS[] = L"AIPixelRecognizerWin";

    WNDCLASSW wc{};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.hbrBackground = ::CreateSolidBrush(RGB(32, 32, 32));
    wc.lpszClassName = CLASS;

    ::RegisterClassW(&wc);

    HWND hwnd = ::CreateWindowExW(
        0, CLASS, L"AI Pixel Recognition Tool",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        200, 200, 780, 560,
        nullptr, nullptr, hInstance, nullptr);

    if (!hwnd)
        return;

    ::ShowWindow(hwnd, cmdShow);
    ::UpdateWindow(hwnd);

    MSG msg{};
    while (::GetMessageW(&msg, nullptr, 0, 0))
    {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }
}
