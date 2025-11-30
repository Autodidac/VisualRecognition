module;
#define NOMINMAX
#include <windows.h>

#include "ids.hpp"

#include <algorithm>
#include <string>
#include <cstring>  // for std::memcpy

export module ui:layout;

import std;
import pixelai;

import :common;
import :capture;
import :filesystem;
import vr.console_log;
import vr.macro.core;
import vr.macro.hooks;

using consolelog::WM_LOG_FLUSH;

namespace ui::detail
{
    // -----------------------------------------------------------------
    // Control creation and layout
    // -----------------------------------------------------------------

    void LayoutControls(HWND hwnd)
    {
        RECT rc{};
        ::GetClientRect(hwnd, &rc);

        const int w = rc.right - rc.left;
        const int h = rc.bottom - rc.top;

        const int margin = 10;
        const int spacing = 8;

        const int btnW = 104;
        const int btnH = 30;
        const int statusH = 26;
        const int logH = 140;

        const int contentW = w - margin * 2;
        const int logTop = h - margin - logH;

        int x = margin;
        int y = margin;

        // Status row anchored to the top for high visibility
        ::MoveWindow(::GetDlgItem(hwnd, IDC_STATUS),
            x, y, contentW, statusH, TRUE);
        y += statusH + spacing;

        // Row 0: Mouse coords + Repeat + Record + Clear + Play + Exit
        const int coordW = 200;
        ::MoveWindow(::GetDlgItem(hwnd, IDC_MACRO_COORDS),
            x, y, coordW, btnH, TRUE);
        x += coordW + spacing;

        ::MoveWindow(::GetDlgItem(hwnd, IDC_MACRO_REPEAT),
            x, y, 90, btnH, TRUE);
        x += 90 + spacing;

        ::MoveWindow(::GetDlgItem(hwnd, IDC_MACRO_RECORD),
            x, y, btnW, btnH, TRUE);
        x += btnW + spacing;

        ::MoveWindow(::GetDlgItem(hwnd, IDC_MACRO_CLEAR),
            x, y, btnW, btnH, TRUE);
        x += btnW + spacing;

        ::MoveWindow(::GetDlgItem(hwnd, IDC_MACRO_PLAY),
            x, y, btnW, btnH, TRUE);
        x += btnW + spacing;

        ::MoveWindow(::GetDlgItem(hwnd, IDC_MACRO_EXIT),
            x, y, btnW, btnH, TRUE);

        // Row 1: AI buttons
        x = margin;
        y += btnH + spacing;

        ::MoveWindow(::GetDlgItem(hwnd, IDC_BTN_CAPTURE),
            x, y, btnW, btnH, TRUE);
        x += btnW + spacing;

        ::MoveWindow(::GetDlgItem(hwnd, IDC_BTN_LEARN),
            x, y, btnW, btnH, TRUE);
        x += btnW + spacing;

        ::MoveWindow(::GetDlgItem(hwnd, IDC_BTN_CLASSIFY),
            x, y, btnW, btnH, TRUE);
        x += btnW + spacing;

        ::MoveWindow(::GetDlgItem(hwnd, IDC_BTN_PREV),
            x, y, btnW, btnH, TRUE);
        x += btnW + spacing;

        ::MoveWindow(::GetDlgItem(hwnd, IDC_BTN_NEXT),
            x, y, btnW, btnH, TRUE);
        x += btnW + spacing;

        ::MoveWindow(::GetDlgItem(hwnd, IDC_BTN_PROMPT),
            x, y, btnW, btnH, TRUE);
        x += btnW + spacing;

        ::MoveWindow(::GetDlgItem(hwnd, IDC_BTN_DELETE),
            x, y, btnW, btnH, TRUE);
        x += btnW + spacing;

        ::MoveWindow(::GetDlgItem(hwnd, IDC_BTN_CLEAR_HISTORY),
            x, y, btnW, btnH, TRUE);

        // Preview + history column
        y += btnH + spacing;
        x = margin;

        const int previewTop = y;
        const int availablePreview = (logTop - spacing) - previewTop;
        const int previewH = (std::max)(availablePreview, 200);
        const int previewW = previewH;

        ::MoveWindow(::GetDlgItem(hwnd, IDC_PREVIEW),
            x, y, previewW, previewH, TRUE);

        x += previewW + spacing;

        ::MoveWindow(::GetDlgItem(hwnd, IDC_HISTORY),
            x, y, w - x - margin, btnH, TRUE);

        // Log spans the width at the bottom for readability
        ::MoveWindow(::GetDlgItem(hwnd, IDC_LOG_EDIT),
            margin, logTop, w - margin * 2, logH, TRUE);
    }


    // -----------------------------------------------------------------
    // PromptLabel helper
    // -----------------------------------------------------------------

    std::wstring PromptLabel(HWND owner)
    {
        const wchar_t* kClassName = L"VR_LABEL_PROMPT";

        static bool classRegistered = false;

        struct State
        {
            std::wstring result;
        };

        State state{};

        if (!classRegistered)
        {
            WNDCLASSW wc{};
            wc.lpfnWndProc = [](HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) -> LRESULT
                {
                    State* st = reinterpret_cast<State*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));

                    switch (msg)
                    {
                    case WM_NCCREATE:
                    {
                        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
                        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                            reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
                    }
                    return TRUE;
                    case WM_COMMAND:
                        if (LOWORD(wp) == IDOK)
                        {
                            if (st)
                            {
                                wchar_t buf[256]{};
                                ::GetWindowTextW(::GetDlgItem(hwnd, 100),
                                    buf, 255);
                                st->result = buf;
                            }
                            ::EndDialog(hwnd, IDOK);
                            return 0;
                        }
                        if (LOWORD(wp) == IDCANCEL)
                        {
                            ::EndDialog(hwnd, IDCANCEL);
                            return 0;
                        }
                        break;
                    case WM_CLOSE:
                        ::EndDialog(hwnd, IDCANCEL);
                        return 0;
                    }
                    return ::DefWindowProcW(hwnd, msg, wp, lp);
                };
            wc.hInstance = ::GetModuleHandleW(nullptr);
            wc.lpszClassName = kClassName;
            wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
            ::RegisterClassW(&wc);
            classRegistered = true;
        }

        constexpr int dlgW = 320;
        constexpr int dlgH = 140;

        HWND dlg = ::CreateWindowExW(
            WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
            kClassName,
            L"Enter label",
            WS_CAPTION | WS_SYSMENU,
            CW_USEDEFAULT, CW_USEDEFAULT,
            dlgW, dlgH,
            owner,
            nullptr,
            ::GetModuleHandleW(nullptr),
            &state);

        if (!dlg)
            return {};

        ::CreateWindowExW(
            0, L"STATIC", L"Label:",
            WS_CHILD | WS_VISIBLE,
            12, 12, dlgW - 24, 20,
            dlg, nullptr,
            ::GetModuleHandleW(nullptr), nullptr);

        ::CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            12, 36, dlgW - 24, 24,
            dlg, reinterpret_cast<HMENU>(100),
            ::GetModuleHandleW(nullptr), nullptr);

        ::CreateWindowExW(
            0, L"BUTTON", L"OK",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            dlgW - 180, dlgH - 40, 80, 24,
            dlg, reinterpret_cast<HMENU>(IDOK),
            ::GetModuleHandleW(nullptr), nullptr);

        ::CreateWindowExW(
            0, L"BUTTON", L"Cancel",
            WS_CHILD | WS_VISIBLE,
            dlgW - 92, dlgH - 40, 80, 24,
            dlg, reinterpret_cast<HMENU>(IDCANCEL),
            ::GetModuleHandleW(nullptr), nullptr);

        ::ShowWindow(dlg, SW_SHOW);
        ::UpdateWindow(dlg);

        MSG msg{};
        while (::IsWindow(dlg) &&
            ::GetMessageW(&msg, nullptr, 0, 0))
        {
            if (!::IsDialogMessageW(dlg, &msg))
            {
                ::TranslateMessage(&msg);
                ::DispatchMessageW(&msg);
            }
        }

        return state.result;
    }

    // -----------------------------------------------------------------
    // Painting for preview (owner-draw STATIC via WM_DRAWITEM)
    // -----------------------------------------------------------------

    void PaintPreview(const DRAWITEMSTRUCT* dis)
    {
        HDC  hdc = dis->hDC;
        RECT rc = dis->rcItem;

        // Background
        HBRUSH back = ::CreateSolidBrush(RGB(16, 16, 16));
        ::FillRect(hdc, &rc, back);
        ::DeleteObject(back);

        const Capture* cap = CurrentCapture();
        if (!cap || cap->width <= 0 || cap->height <= 0)
            return;

        const int w = cap->width;
        const int h = cap->height;

        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = w;
        bmi.bmiHeader.biHeight = -h;   // topdown
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        void* bits = nullptr;
        HBITMAP hbm = ::CreateDIBSection(
            hdc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);

        if (!hbm || !bits)
        {
            if (hbm) ::DeleteObject(hbm);
            return;
        }

        const std::size_t bytes =
            static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u;
        std::memcpy(bits, cap->pixels.data(), bytes);

        HDC hdcMem = ::CreateCompatibleDC(hdc);
        HGDIOBJ old = ::SelectObject(hdcMem, hbm);

        const int dstW = rc.right - rc.left;
        const int dstH = rc.bottom - rc.top;

        ::SetStretchBltMode(hdc, HALFTONE);
        ::StretchBlt(
            hdc,
            rc.left, rc.top, dstW, dstH,
            hdcMem,
            0, 0, w, h,
            SRCCOPY);

        ::SelectObject(hdcMem, old);
        ::DeleteDC(hdcMem);
        ::DeleteObject(hbm);
    }

    // -----------------------------------------------------------------
    // Create all controls
    // -----------------------------------------------------------------

    void CreateChildControls(HWND hwnd)
    {
        g_status = ::CreateWindowExW(
            0, L"STATIC", L"Ready.",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            0, 0, 0, 0,
            hwnd,
            reinterpret_cast<HMENU>(IDC_STATUS),
            ::GetModuleHandleW(nullptr),
            nullptr);

        g_preview = ::CreateWindowExW(
            WS_EX_CLIENTEDGE, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
            0, 0, 0, 0,
            hwnd,
            reinterpret_cast<HMENU>(IDC_PREVIEW),
            ::GetModuleHandleW(nullptr),
            nullptr);

        g_historyLabel = ::CreateWindowExW(
            0, L"STATIC", L"No captures yet.",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            0, 0, 0, 0,
            hwnd,
            reinterpret_cast<HMENU>(IDC_HISTORY),
            ::GetModuleHandleW(nullptr),
            nullptr);

        // Macro controls
        ::CreateWindowExW(
            0, L"STATIC", L"Mouse: (0,0)",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            0, 0, 0, 0,
            hwnd,
            reinterpret_cast<HMENU>(IDC_MACRO_COORDS),
            ::GetModuleHandleW(nullptr),
            nullptr);

        ::CreateWindowExW(
            0, L"BUTTON", L"Repeat",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            0, 0, 0, 0,
            hwnd,
            reinterpret_cast<HMENU>(IDC_MACRO_REPEAT),
            ::GetModuleHandleW(nullptr),
            nullptr);

        ::CreateWindowExW(
            0, L"BUTTON", L"Record",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 0, 0,
            hwnd,
            reinterpret_cast<HMENU>(IDC_MACRO_RECORD),
            ::GetModuleHandleW(nullptr),
            nullptr);

        ::CreateWindowExW(
            0, L"BUTTON", L"Clear",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 0, 0,
            hwnd,
            reinterpret_cast<HMENU>(IDC_MACRO_CLEAR),
            ::GetModuleHandleW(nullptr),
            nullptr);

        ::CreateWindowExW(
            0, L"BUTTON", L"Play",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 0, 0,
            hwnd,
            reinterpret_cast<HMENU>(IDC_MACRO_PLAY),
            ::GetModuleHandleW(nullptr),
            nullptr);

        ::CreateWindowExW(
            0, L"BUTTON", L"Exit",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 0, 0,
            hwnd,
            reinterpret_cast<HMENU>(IDC_MACRO_EXIT),
            ::GetModuleHandleW(nullptr),
            nullptr);

        // AI controls
        ::CreateWindowExW(
            0, L"BUTTON", L"Capture",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 0, 0,
            hwnd,
            reinterpret_cast<HMENU>(IDC_BTN_CAPTURE),
            ::GetModuleHandleW(nullptr),
            nullptr);

        ::CreateWindowExW(
            0, L"BUTTON", L"Learn...",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 0, 0,
            hwnd,
            reinterpret_cast<HMENU>(IDC_BTN_LEARN),
            ::GetModuleHandleW(nullptr),
            nullptr);

        ::CreateWindowExW(
            0, L"BUTTON", L"Classify",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 0, 0,
            hwnd,
            reinterpret_cast<HMENU>(IDC_BTN_CLASSIFY),
            ::GetModuleHandleW(nullptr),
            nullptr);

        ::CreateWindowExW(
            0, L"BUTTON", L"Prev",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 0, 0,
            hwnd,
            reinterpret_cast<HMENU>(IDC_BTN_PREV),
            ::GetModuleHandleW(nullptr),
            nullptr);

        ::CreateWindowExW(
            0, L"BUTTON", L"Next",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 0, 0,
            hwnd,
            reinterpret_cast<HMENU>(IDC_BTN_NEXT),
            ::GetModuleHandleW(nullptr),
            nullptr);

        ::CreateWindowExW(
            0, L"BUTTON", L"Prompt label",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 0, 0,
            hwnd,
            reinterpret_cast<HMENU>(IDC_BTN_PROMPT),
            ::GetModuleHandleW(nullptr),
            nullptr);

        ::CreateWindowExW(
            0, L"BUTTON", L"Delete",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 0, 0,
            hwnd,
            reinterpret_cast<HMENU>(IDC_BTN_DELETE),
            ::GetModuleHandleW(nullptr),
            nullptr);

        ::CreateWindowExW(
            0, L"BUTTON", L"Clear preview",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 0, 0,
            hwnd,
            reinterpret_cast<HMENU>(IDC_BTN_CLEAR_HISTORY),
            ::GetModuleHandleW(nullptr),
            nullptr);

        // Log window
        g_logEdit = ::CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_MULTILINE |
            ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
            0, 0, 0, 0,
            hwnd,
            reinterpret_cast<HMENU>(IDC_LOG_EDIT),
            ::GetModuleHandleW(nullptr),
            nullptr);

        consolelog::init(hwnd, g_logEdit);

        UpdateHistoryLabel();
    }

    // -----------------------------------------------------------------
    // Status: mouse coords label
    // -----------------------------------------------------------------

    void UpdateMouseCoordsLabel(HWND hwnd)
    {
        POINT pt{};
        if (!::GetCursorPos(&pt))
            return;

        wchar_t buf[64]{};
        std::swprintf(buf, 64, L"Mouse: (%ld,%ld)",
            static_cast<long>(pt.x),
            static_cast<long>(pt.y));

        ::SetWindowTextW(::GetDlgItem(hwnd, IDC_MACRO_COORDS), buf);
    }

    // -----------------------------------------------------------------
    // Main window procedure
    // -----------------------------------------------------------------

    LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg)
        {
        case WM_CREATE:
            g_mainWindow = hwnd;
            CreateChildControls(hwnd);
            LayoutControls(hwnd);
            UpdateMouseCoordsLabel(hwnd);
            ::SetTimer(hwnd, 1, 100, nullptr); // mouse coord refresh
            return 0;

        case WM_USER + 101:
        {
            // Perform full capture safely on UI thread
            ui::detail::DoCapture();
            return 0;
        }

        case WM_SIZE:
            LayoutControls(hwnd);
            return 0;

        case WM_TIMER:
            if (wParam == 1)
            {
                UpdateMouseCoordsLabel(hwnd);
            }
            return 0;

        case WM_COMMAND:
            switch (LOWORD(wParam))
            {
            case IDC_MACRO_RECORD:
                macro::toggle_record();
                return 0;
            case IDC_MACRO_CLEAR:
                macro::clear_macro();
                return 0;
            case IDC_MACRO_PLAY:
                macro::toggle_play();
                return 0;
            case IDC_MACRO_REPEAT:
            {
                const BOOL checked =
                    (::SendMessageW(::GetDlgItem(hwnd, IDC_MACRO_REPEAT),
                        BM_GETCHECK, 0, 0) == BST_CHECKED);
                macro::set_repeat(checked != FALSE);
            }
            return 0;
            case IDC_MACRO_EXIT:
            case IDC_BTN_EXIT:
                macro::request_exit();
                ::PostQuitMessage(0);
                return 0;

            case IDC_BTN_CAPTURE:
                DoCapture();
                return 0;
            case IDC_BTN_LEARN:
            case IDC_BTN_PROMPT:
            {
                const std::wstring label = PromptLabel(hwnd);
                if (!label.empty())
                    DoLearnFromSelected(label);
            }
            return 0;
            case IDC_BTN_CLASSIFY:
                DoClassify();
                return 0;
            case IDC_BTN_PREV:
                SelectPreviousCapture();
                return 0;
            case IDC_BTN_NEXT:
                SelectNextCapture();
                return 0;
            case IDC_BTN_DELETE:
                DeleteSelectedCapture();
                return 0;
            case IDC_BTN_CLEAR_HISTORY:
                ClearHistory();
                return 0;
            default:
                break;
            }
            break;

        case WM_DRAWITEM:
        {
            auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
            if (dis && dis->CtlID == IDC_PREVIEW)
            {
                PaintPreview(dis);
                return TRUE;
            }
        }
        break;

        case WM_LOG_FLUSH:
            consolelog::flush_to_edit();
            return 0;

        case WM_DESTROY:
            macro::request_exit();
            ::PostQuitMessage(0);
            return 0;
        }

        return ::DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    // -----------------------------------------------------------------
    // Exported helper to create main window
    // -----------------------------------------------------------------

    export HWND CreateAndShowMainWindow(HINSTANCE instance, int cmdShow)
    {
        const wchar_t* kClassName = L"VisualRecognition_Main";

        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = instance;
        wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszClassName = kClassName;

        if (!::RegisterClassExW(&wc) && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            return nullptr;

        HWND hwnd = ::CreateWindowExW(
            0,
            kClassName,
            L"Visual Recognition + Macro",
            WS_OVERLAPPEDWINDOW | WS_VISIBLE,
            CW_USEDEFAULT, CW_USEDEFAULT,
            1024, 768,
            nullptr,
            nullptr,
            instance,
            nullptr);

        if (!hwnd)
            return nullptr;

        ::ShowWindow(hwnd, cmdShow);
        ::UpdateWindow(hwnd);
        return hwnd;
    }
}
