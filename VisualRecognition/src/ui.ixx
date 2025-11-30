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

export module ui;

import std;
import pixelai;

using almond::pixelai::PixelRecognizer;

namespace
{
    constexpr int  kCaptureRadius = 240;
    constexpr char kModelFile[] = "pixelai_examples.bin";
    constexpr int  kDefaultBackupRetention = 5;

    HWND g_status = nullptr;
    HWND g_preview = nullptr;
    HWND g_historyLabel = nullptr;
    HWND g_mainWindow = nullptr;
    HHOOK g_mouseHook = nullptr;
    HHOOK g_keyboardHook = nullptr;
    bool g_mouseHookPaused = false;

    PixelRecognizer g_ai{};

    struct Capture
    {
        std::vector<std::uint32_t> pixels;
        int width{};
        int height{};
        std::chrono::system_clock::time_point timestamp{};
    };

    std::vector<Capture> g_history;
    int g_selectedIndex = -1;

    // -------------------------------------------------------------------------
    // Filesystem helpers
    // -------------------------------------------------------------------------
    std::filesystem::path GetHistoryDir()
    {
        wchar_t buffer[MAX_PATH]{};
        DWORD len = ::GetTempPathW(static_cast<DWORD>(std::size(buffer)), buffer);
        if (len == 0 || len > std::size(buffer))
        {
            return std::filesystem::temp_directory_path() / "pixelai_captures";
        }

        std::filesystem::path dir(buffer);
        dir /= "pixelai_captures";
        return dir;
    }

    std::filesystem::path GetModelPath()
    {
        std::error_code ec{};
        auto cwd = std::filesystem::current_path(ec);
        if (ec)
            return std::filesystem::path(kModelFile);

        return cwd / kModelFile;
    }

    std::filesystem::path GetSettingsPath()
    {
        wchar_t buffer[MAX_PATH]{};
        DWORD len = ::GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
        if (len == 0 || len >= std::size(buffer))
            return std::filesystem::path(L"pixelai.ini");

        std::filesystem::path exePath(buffer);
        return exePath.parent_path() / L"pixelai.ini";
    }

    int GetBackupRetention()
    {
        auto ini = GetSettingsPath();
        if (std::filesystem::exists(ini))
        {
            int value = static_cast<int>(::GetPrivateProfileIntW(
                L"Saving",
                L"BackupRetention",
                kDefaultBackupRetention,
                ini.c_str()));
            return std::max(0, value);
        }

        return kDefaultBackupRetention;
    }

    std::optional<std::filesystem::path> CreateModelBackup(const std::filesystem::path& modelPath)
    {
        if (!std::filesystem::exists(modelPath))
            return std::nullopt;

        std::error_code ec{};
        std::filesystem::create_directories(modelPath.parent_path(), ec);

        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);
        tm localTm{};
        localtime_s(&localTm, &t);

        wchar_t ts[32]{};
        std::wcsftime(ts, std::size(ts), L"%Y%m%d_%H%M%S", &localTm);

        auto backupName = modelPath.stem().wstring() + L"_" + ts + modelPath.extension().wstring();
        auto backupPath = modelPath.parent_path() / backupName;

        std::filesystem::copy_file(modelPath, backupPath, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec)
            return std::nullopt;

        return backupPath;
    }

    void EnforceBackupRetention(const std::filesystem::path& modelPath)
    {
        int retention = GetBackupRetention();
        if (retention <= 0)
            return;

        std::vector<std::filesystem::directory_entry> backups;
        std::error_code ec{};
        for (auto& entry : std::filesystem::directory_iterator(modelPath.parent_path(), ec))
        {
            if (!entry.is_regular_file())
                continue;

            auto name = entry.path().filename().wstring();
            if (name.starts_with(modelPath.stem().wstring() + L"_") && entry.path().extension() == modelPath.extension())
                backups.push_back(entry);
        }

        std::sort(backups.begin(), backups.end(), [](const auto& a, const auto& b)
            {
                std::error_code aec{};
                std::error_code bec{};
                return a.last_write_time(aec) < b.last_write_time(bec);
            });

        if (backups.size() <= static_cast<std::size_t>(retention))
            return;

        auto toRemove = backups.size() - static_cast<std::size_t>(retention);
        for (std::size_t i = 0; i < toRemove; ++i)
        {
            std::filesystem::remove(backups[i]);
        }
    }

    bool SaveCaptureToDisk(const Capture& cap)
    {
        auto dir = GetHistoryDir();
        std::error_code ec{};
        std::filesystem::create_directories(dir, ec);

        auto ticks = std::chrono::duration_cast<std::chrono::milliseconds>(cap.timestamp.time_since_epoch()).count();
        auto file = dir / (std::to_wstring(ticks) + L".bin");

        int dedupe = 1;
        while (std::filesystem::exists(file))
        {
            file = dir / (std::to_wstring(ticks) + L"_" + std::to_wstring(dedupe) + L".bin");
            ++dedupe;
        }

        std::ofstream ofs(file, std::ios::binary | std::ios::trunc);
        if (!ofs)
            return false;

        std::int32_t w = cap.width;
        std::int32_t h = cap.height;
        std::int64_t t = ticks;
        std::uint64_t count = cap.pixels.size();

        ofs.write(reinterpret_cast<const char*>(&w), sizeof(w));
        ofs.write(reinterpret_cast<const char*>(&h), sizeof(h));
        ofs.write(reinterpret_cast<const char*>(&t), sizeof(t));
        ofs.write(reinterpret_cast<const char*>(&count), sizeof(count));
        ofs.write(reinterpret_cast<const char*>(cap.pixels.data()), static_cast<std::streamsize>(count * sizeof(std::uint32_t)));

        return ofs.good();
    }

    std::optional<Capture> LoadCaptureFromDisk(const std::filesystem::path& path)
    {
        std::ifstream ifs(path, std::ios::binary);
        if (!ifs)
            return std::nullopt;

        std::int32_t w{};
        std::int32_t h{};
        std::int64_t t{};
        std::uint64_t count{};

        ifs.read(reinterpret_cast<char*>(&w), sizeof(w));
        ifs.read(reinterpret_cast<char*>(&h), sizeof(h));
        ifs.read(reinterpret_cast<char*>(&t), sizeof(t));
        ifs.read(reinterpret_cast<char*>(&count), sizeof(count));

        if (w <= 0 || h <= 0 || count == 0)
            return std::nullopt;

        std::uint64_t expected = static_cast<std::uint64_t>(w) * static_cast<std::uint64_t>(h);
        if (count != expected)
            return std::nullopt;

        std::vector<std::uint32_t> pixels;
        pixels.resize(count);
        ifs.read(reinterpret_cast<char*>(pixels.data()), static_cast<std::streamsize>(count * sizeof(std::uint32_t)));
        if (!ifs)
            return std::nullopt;

        Capture cap{};
        cap.width = w;
        cap.height = h;
        cap.timestamp = std::chrono::system_clock::time_point{ std::chrono::milliseconds{ t } };
        cap.pixels = std::move(pixels);
        return cap;
    }

    void LoadCaptureHistory()
    {
        g_history.clear();
        g_selectedIndex = -1;

        auto dir = GetHistoryDir();
        if (!std::filesystem::exists(dir))
            return;

        std::vector<Capture> loaded;
        for (auto& entry : std::filesystem::directory_iterator(dir))
        {
            if (!entry.is_regular_file())
                continue;

            auto cap = LoadCaptureFromDisk(entry.path());
            if (cap)
                loaded.push_back(std::move(*cap));
        }

        std::sort(loaded.begin(), loaded.end(), [](const Capture& a, const Capture& b)
            {
                return a.timestamp < b.timestamp;
            });

        g_history = std::move(loaded);
        if (!g_history.empty())
            g_selectedIndex = static_cast<int>(g_history.size() - 1);
    }

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

    std::wstring FormatBackupPath(const std::filesystem::path& path)
    {
        std::error_code ec{};
        auto absolutePath = std::filesystem::absolute(path, ec);
        const auto& displayPath = ec ? path : absolutePath;
        return displayPath.wstring();
    }

    const Capture* CurrentCapture()
    {
        if (g_selectedIndex < 0 || g_selectedIndex >= static_cast<int>(g_history.size()))
            return nullptr;
        return &g_history[static_cast<std::size_t>(g_selectedIndex)];
    }

    std::wstring FormatTimestamp(const std::chrono::system_clock::time_point& tp)
    {
        auto t = std::chrono::system_clock::to_time_t(tp);
        tm localTm{};
        localtime_s(&localTm, &t);

        wchar_t buffer[64]{};
        std::wcsftime(buffer, std::size(buffer), L"%Y-%m-%d %H:%M:%S", &localTm);
        return buffer;
    }

    void UpdateHistoryLabel()
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

    void SelectCapture(int index)
    {
        if (index < 0 || index >= static_cast<int>(g_history.size()))
            return;
        g_selectedIndex = index;
        UpdateHistoryLabel();
        if (g_preview)
            ::InvalidateRect(g_preview, nullptr, TRUE);
    }

    // -------------------------------------------------------------------------
    // Real screen capture around cursor (multi-monitor safe)
    // -------------------------------------------------------------------------
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
            if (g_mouseHookPaused)
                return ::CallNextHookEx(g_mouseHook, code, wParam, lParam);

            auto belongsToMainWindow = [](HWND hwnd)
            {
                if (!hwnd || !g_mainWindow)
                    return false;

                HWND root = ::GetAncestor(hwnd, GA_ROOT);
                return root == g_mainWindow;
            };

            MSLLHOOKSTRUCT* info = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);
            HWND hitWindow = info ? ::WindowFromPoint(info->pt) : nullptr;
            HWND foreground = ::GetForegroundWindow();

            if (belongsToMainWindow(foreground) || belongsToMainWindow(hitWindow))
                return ::CallNextHookEx(g_mouseHook, code, wParam, lParam);

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

    // -------------------------------------------------------------------------
    // Owner-draw preview
    // -------------------------------------------------------------------------
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

    // -------------------------------------------------------------------------
    // Capture + classify logic (buttons + global hook)
    // -------------------------------------------------------------------------
    void DoCapture()
    {
        auto cap = CapturePatchAroundCursor();
        if (cap)
        {
            g_history.push_back(std::move(*cap));
            g_selectedIndex = static_cast<int>(g_history.size() - 1);
            bool saved = SaveCaptureToDisk(g_history.back());
            UpdateHistoryLabel();
            if (saved)
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

    // -------------------------------------------------------------------------
    // Main window proc
    // -------------------------------------------------------------------------
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

            LoadCaptureHistory();
            UpdateHistoryLabel();
            if (!g_history.empty())
            {
                SetStatus(L"Loaded capture history from disk.");
                if (g_preview)
                    ::InvalidateRect(g_preview, nullptr, TRUE);
            }

            auto modelPath = GetModelPath();
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

            case IDC_BTN_PREV:
                SelectPreviousCapture();
                break;

            case IDC_BTN_NEXT:
                SelectNextCapture();
                break;

            case IDC_BTN_LEARN:
            {
                const auto* cap = CurrentCapture();
                if (!cap)
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

                std::string label = narrow_utf8(labelW);

                auto modelPath = GetModelPath();
                bool modelExists = std::filesystem::exists(modelPath);

                if (modelExists)
                {
                    std::wstring prompt = L"An existing model file was found at:\n\n";
                    prompt += modelPath.wstring();
                    prompt += L"\n\nCreate a timestamped backup and overwrite it with the new training data?";

                    int res = ::MessageBoxW(hwnd, prompt.c_str(), L"Confirm Save", MB_ICONQUESTION | MB_YESNO | MB_DEFBUTTON2);
                    if (res != IDYES)
                    {
                        SetStatus(L"Save cancelled; existing model preserved.");
                        break;
                    }
                }

                if (g_ai.add_example_bgra32(cap->pixels, cap->width, cap->height, label))
                {
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
