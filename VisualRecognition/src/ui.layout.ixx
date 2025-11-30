module;
#define NOMINMAX
#include <windows.h>

#include "ids.hpp"

#include <string>
#include <utility>
#include <filesystem>
#include <sstream>

export module ui:layout;

import std;
import pixelai;
import :common;
import :filesystem;
import :capture;
import :hooks;

namespace ui::detail
{
    // Label entry mini-dialog
    struct PromptState
    {
        HWND hwnd{};
        HWND edit{};
        std::wstring result;
        bool done{ false };
        bool accepted{ false };
    };

    // Deliberately non-inline to ensure a concrete definition is emitted for the linker.
    PromptState g_prompt{};

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
        bool previousHookPaused = std::exchange(g_mouseHookPaused, true);

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
        {
            g_mouseHookPaused = previousHookPaused;
            return g_prompt.result;
        }
        g_mouseHookPaused = previousHookPaused;
        return {};
    }

    // Layout helpers
    inline void LayoutControls(HWND hwnd)
    {
        RECT rc{};
        ::GetClientRect(hwnd, &rc);

        int W = rc.right - rc.left;
        int H = rc.bottom - rc.top;

        const int margin = 14;
        const int spacing = 10;
        const int btnW = 130;
        const int btnH = 42;
        const int statusH = 38;
        const int historyH = 22;

        int x = margin;
        int y = margin;

        ::MoveWindow(::GetDlgItem(hwnd, IDC_BTN_CAPTURE), x, y, btnW, btnH, TRUE);
        x += btnW + spacing;

        ::MoveWindow(::GetDlgItem(hwnd, IDC_BTN_LEARN), x, y, btnW, btnH, TRUE);
        x += btnW + spacing;

        ::MoveWindow(::GetDlgItem(hwnd, IDC_BTN_CLASSIFY), x, y, btnW, btnH, TRUE);
        x += btnW + spacing;

        ::MoveWindow(::GetDlgItem(hwnd, IDC_BTN_PROMPT), x, y, btnW, btnH, TRUE);

        y += btnH + spacing;
        x = margin;

        ::MoveWindow(::GetDlgItem(hwnd, IDC_BTN_PREV), x, y, btnW, btnH, TRUE);
        x += btnW + spacing;

        ::MoveWindow(::GetDlgItem(hwnd, IDC_BTN_NEXT), x, y, btnW, btnH, TRUE);
        x += btnW + spacing;

        ::MoveWindow(::GetDlgItem(hwnd, IDC_BTN_DELETE), x, y, btnW, btnH, TRUE);
        x += btnW + spacing;

        ::MoveWindow(::GetDlgItem(hwnd, IDC_BTN_EXIT), x, y, btnW, btnH, TRUE);

        int infoY = y + btnH + spacing;
        if (g_historyLabel)
        {
            ::MoveWindow(g_historyLabel,
                margin,
                infoY,
                W - margin * 2,
                historyH,
                TRUE);
        }

        int previewY = infoY + historyH + spacing;

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

    // Owner-draw preview
    void DrawPreview(const DRAWITEMSTRUCT& dis)
    {
        HDC  hdc = dis.hDC;
        RECT rc = dis.rcItem;

        ::FillRect(hdc, &rc, reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1));

        auto current = CurrentCapture();
        if (!current)
            return;

        const Capture& cap = *current;

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

    // Main window proc
    LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg)
        {
        case WM_CREATE:
        {
            g_mainWindow = hwnd;

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

            ::CreateWindowW(L"BUTTON", L"Prompt Label",
                WS_VISIBLE | WS_CHILD,
                0, 0, 0, 0,
                hwnd, (HMENU)IDC_BTN_PROMPT,
                nullptr, nullptr);

            ::CreateWindowW(L"BUTTON", L"Prev Capture",
                WS_VISIBLE | WS_CHILD,
                0, 0, 0, 0,
                hwnd, (HMENU)IDC_BTN_PREV,
                nullptr, nullptr);

            ::CreateWindowW(L"BUTTON", L"Next Capture",
                WS_VISIBLE | WS_CHILD,
                0, 0, 0, 0,
                hwnd, (HMENU)IDC_BTN_NEXT,
                nullptr, nullptr);

            ::CreateWindowW(L"BUTTON", L"Delete Capture",
                WS_VISIBLE | WS_CHILD,
                0, 0, 0, 0,
                hwnd, (HMENU)IDC_BTN_DELETE,
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

            g_historyLabel = ::CreateWindowW(L"STATIC", L"No captures yet",
                WS_VISIBLE | WS_CHILD | SS_LEFT,
                0, 0, 0, 0,
                hwnd, (HMENU)IDC_HISTORY,
                nullptr, nullptr);

            g_status = ::CreateWindowW(L"STATIC", L"Ready",
                WS_VISIBLE | WS_CHILD | SS_LEFT,
                0, 0, 0, 0,
                hwnd, (HMENU)IDC_STATUS,
                nullptr, nullptr);

            if (!EnsureSettingsFile())
            {
                SetStatus(L"Failed to create default settings file.");
            }

            LoadCaptureHistory();
            UpdateHistoryLabel();
            if (!g_history.empty())
            {
                SetStatus(L"Loaded capture history from disk.");
                if (g_preview)
                    ::InvalidateRect(g_preview, nullptr, TRUE);
            }

            auto modelPath = GetModelPath();
            if (!std::filesystem::exists(modelPath))
            {
                if (CreatePlaceholderModelFile(modelPath))
                {
                    SetStatus(L"Model file missing; created placeholder.");
                }
                else
                {
                    SetStatus(L"Model file missing and could not be created.");
                }
            }

            if (!g_ai.load_from_file(modelPath.string()))
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
            if (wParam == IDC_PREVIEW)
            {
                auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
                if (dis)
                    DrawPreview(*dis);
                return TRUE;
            }
            break;

        case WM_COMMAND:
        {
            switch (LOWORD(wParam))
            {
            case IDC_BTN_CAPTURE:
            {
                DoCapture();
                break;
            }

            case IDC_BTN_CLASSIFY:
                DoClassify();
                break;

            case IDC_BTN_PREV:
                SelectPreviousCapture();
                break;

            case IDC_BTN_NEXT:
                SelectNextCapture();
                break;

            case IDC_BTN_DELETE:
                DeleteSelectedCapture();
                break;

            case IDC_BTN_LEARN:
            {
                auto* cap = CurrentCapture();
                if (!cap)
                {
                    SetStatus(L"No capture to learn from.");
                    break;
                }

                std::wstring label = PromptLabel(hwnd);
                if (label.empty())
                {
                    SetStatus(L"No label entered.");
                    break;
                }

                std::string labelUtf8 = narrow_utf8(label);
                auto patchSize = g_ai.patch_size();
                int targetW = patchSize.first;
                int targetH = patchSize.second;

                if (targetW <= 0 || targetH <= 0)
                {
                    targetW = cap->width;
                    targetH = cap->height;
                }

                if (cap->width >= targetW && cap->height >= targetH)
                {
                    if (!g_ai.add_example_bgra32(cap->pixels, cap->width, cap->height, labelUtf8))
                    {
                        SetStatus(L"Training failed (size mismatch?).");
                        break;
                    }

                    auto modelPath = GetModelPath();
                    bool modelExists = std::filesystem::exists(modelPath);

                    std::optional<std::filesystem::path> backupPath;
                    if (modelExists)
                    {
                        backupPath = CreateModelBackup(modelPath);
                        if (!backupPath)
                        {
                            SetStatus(L"Failed to create model backup; save aborted.");
                            break;
                        }

                        EnforceBackupRetention(modelPath);
                    }

                    if (g_ai.save_to_file(modelPath.string()))
                    {
                        std::wstring saveStatus = L"Example learned + saved to ";
                        saveStatus += modelPath.wstring();
                        if (backupPath)
                        {
                            saveStatus += L" (backup: ";
                            saveStatus += FormatBackupPath(*backupPath);
                            saveStatus += L")";
                        }
                        SetStatus(saveStatus);
                    }
                    else
                    {
                        std::wstring failStatus = L"Learned, but failed to save model file to ";
                        failStatus += modelPath.wstring();
                        SetStatus(failStatus);
                    }
                }
                else
                {
                    SetStatus(L"Training failed (size mismatch?).");
                }

                break;
            }

            case IDC_BTN_PROMPT:
            {
                const auto* cap = CurrentCapture();
                if (!cap)
                {
                    SetStatus(L"No capture selected for labeling.");
                    break;
                }

                std::wstring labelW = PromptLabel(hwnd);
                if (labelW.empty())
                {
                    SetStatus(L"Label entry cancelled.");
                    break;
                }

                std::wstring status = L"Label recorded: ";
                status += labelW;
                SetStatus(status);
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
            g_mainWindow = nullptr;
            ::PostQuitMessage(0);
            return 0;
        }

        return ::DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

// Exported entrypoint used by main.cpp
export void RunUI(HINSTANCE hInstance, int cmdShow)
{
    const wchar_t CLASS[] = L"AIPixelRecognizerWin";

    WNDCLASSW wc{};
    wc.lpfnWndProc = ui::detail::WindowProc;
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
