module;

#define NOMINMAX
#include <windows.h>

export module vr.macro.types;

import std;
import vr.console_log;

namespace macro
{
    // ============================================================
    // 1. CORE TYPES
    // ============================================================

    export struct POINTF
    {
        float x{};
        float y{};
    };

    export struct KeyEvent
    {
        WORD  vk{};
        bool  down{};
        DWORD delay{};
    };

    export struct MouseEvent
    {
        int  x{}, y{};
        bool down{};
        DWORD delay{};

        std::string windowClass;
        std::string windowTitle;

        int relX{}, relY{};
        int winW{}, winH{};
    };

    export using Event = std::variant<KeyEvent, MouseEvent>;
    export using Clock = std::chrono::steady_clock;

    // ============================================================
    // 2. GLOBALS
    // ============================================================

    export inline std::vector<Event>      g_macro{};
    export inline std::mutex              g_lock{};
    export inline std::atomic<bool>       g_record{ false };
    export inline std::atomic<bool>       g_play{ false };
    export inline std::atomic<bool>       g_exit{ false };
    export inline std::atomic<bool>       g_resetMouse{ false };
    export inline std::atomic<long long>  g_lastTick{ 0 };
    export inline std::atomic<bool>       g_repeatEnabled{ false };
    export inline std::atomic<bool>       g_injectedLeftDown{ false };

    // ============================================================
    // 3. HELPERS
    // ============================================================

    export inline long long nowMs() noexcept
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            Clock::now().time_since_epoch()).count();
    }

    export inline void resetTick() noexcept
    {
        g_lastTick.store(nowMs());
    }

    export inline std::string narrow_from_wide(const std::wstring& ws)
    {
        if (ws.empty()) return {};
        const int len = ::WideCharToMultiByte(CP_UTF8, 0, ws.data(), (int)ws.size(), nullptr, 0, nullptr, nullptr);
        if (len <= 0) return {};
        std::string out((size_t)len, '\0');
        ::WideCharToMultiByte(CP_UTF8, 0, ws.data(), (int)ws.size(), out.data(), len, nullptr, nullptr);
        return out;
    }

    export inline std::string narrow_from_wide(const wchar_t* w)
    {
        if (!w || !*w) return {};
        const int lenW = ::lstrlenW(w);
        const int len = ::WideCharToMultiByte(CP_UTF8, 0, w, lenW, nullptr, 0, nullptr, nullptr);
        if (len <= 0) return {};
        std::string out((size_t)len, '\0');
        ::WideCharToMultiByte(CP_UTF8, 0, w, lenW, out.data(), len, nullptr, nullptr);
        return out;
    }

    inline bool is_extended_vk(WORD vk) noexcept
    {
        switch (vk) {
        case VK_INSERT: case VK_DELETE: case VK_HOME: case VK_END:
        case VK_PRIOR: case VK_NEXT: case VK_LEFT: case VK_RIGHT:
        case VK_UP: case VK_DOWN: case VK_RCONTROL: case VK_RMENU:
        case VK_LWIN: case VK_RWIN: case VK_APPS: return true;
        default: return false;
        }
    }

    export inline void sendKey(const KeyEvent& e)
    {
        INPUT in{};
        in.type = INPUT_KEYBOARD;
        in.ki.wScan = (WORD)::MapVirtualKeyW(e.vk, MAPVK_VK_TO_VSC);
        in.ki.dwFlags = KEYEVENTF_SCANCODE | (e.down ? 0 : KEYEVENTF_KEYUP);
        if (is_extended_vk(e.vk)) in.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
        ::SendInput(1, &in, sizeof(in));
    }

    export inline HWND find_window_for_mouse(const MouseEvent& ev);

    export inline void sendMouse(const MouseEvent& e)
    {
        HWND target = nullptr;
        if (!e.windowClass.empty() || !e.windowTitle.empty())
            target = find_window_for_mouse(e);
        if (!target) target = ::GetForegroundWindow();

        RECT wr{}, mr{};
        if (target && !::GetWindowRect(target, &wr)) target = nullptr;

        if (target) {
            HMONITOR mon = ::MonitorFromWindow(target, MONITOR_DEFAULTTONEAREST);
            MONITORINFOEXW mi{}; mi.cbSize = sizeof(mi);
            if (::GetMonitorInfoW(mon, &mi)) mr = mi.rcMonitor;
        }
        if (mr.right == 0) {
            mr.left = ::GetSystemMetrics(SM_XVIRTUALSCREEN);
            mr.top = ::GetSystemMetrics(SM_YVIRTUALSCREEN);
            mr.right = mr.left + ::GetSystemMetrics(SM_CXVIRTUALSCREEN);
            mr.bottom = mr.top + ::GetSystemMetrics(SM_CYVIRTUALSCREEN);
        }

        POINT tPt{ e.x, e.y };
        if (target) {
            int cw = wr.right - wr.left, ch = wr.bottom - wr.top;
            if (e.winW > 0 && e.winH > 0 && cw > 0 && ch > 0) {
                tPt.x = wr.left + (int)std::lround(e.relX * ((double)cw / e.winW));
                tPt.y = wr.top + (int)std::lround(e.relY * ((double)ch / e.winH));
            }
            else {
                tPt.x = wr.left + e.relX;
                tPt.y = wr.top + e.relY;
            }
        }

        tPt.x = std::clamp(tPt.x, (long)mr.left, (long)mr.right - 1);
        tPt.y = std::clamp(tPt.y, (long)mr.top, (long)mr.bottom - 1);

        INPUT in{};
        in.type = INPUT_MOUSE;
        in.mi.dx = (tPt.x - mr.left) * 65535 / (mr.right - mr.left);
        in.mi.dy = (tPt.y - mr.top) * 65535 / (mr.bottom - mr.top);
        in.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
        ::SendInput(1, &in, sizeof(in));

        INPUT c{}; c.type = INPUT_MOUSE;
        c.mi.dwFlags = e.down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
        ::SendInput(1, &c, sizeof(c));

        if (e.down) g_injectedLeftDown.store(true);
        else g_injectedLeftDown.store(false);
    }

    // ============================================================
    // 4. WINDOW HELPERS (Implementation)
    // ============================================================

    export inline void capture_window_info(const POINT& pt, MouseEvent& ev)
    {
        HWND h = ::WindowFromPoint(pt);
        if (!h) { ev.relX = ev.x; ev.relY = ev.y; return; }
        HWND root = ::GetAncestor(h, GA_ROOT);
        if (!root) root = h;

        RECT wr{};
        if (::GetWindowRect(root, &wr)) {
            ev.relX = pt.x - wr.left; ev.relY = pt.y - wr.top;
            ev.winW = wr.right - wr.left; ev.winH = wr.bottom - wr.top;
        }
        else {
            ev.relX = ev.x; ev.relY = ev.y;
        }
        wchar_t cB[256]{}, tB[256]{};
        ::GetClassNameW(root, cB, 255); ::GetWindowTextW(root, tB, 255);
        ev.windowClass = narrow_from_wide(cB);
        ev.windowTitle = narrow_from_wide(tB);
    }

    export inline HWND find_window_for_mouse(const MouseEvent& ev)
    {
        if (ev.windowClass.empty() && ev.windowTitle.empty()) return nullptr;
        struct P { const std::string* c; const std::string* t; HWND h; } args{ &ev.windowClass, &ev.windowTitle, nullptr };
        ::EnumWindows([](HWND h, LPARAM l) -> BOOL {
            auto* p = (P*)l; if (p->h) return FALSE;
            wchar_t cB[256]{}, tB[256]{};
            ::GetClassNameW(h, cB, 255); ::GetWindowTextW(h, tB, 255);
            if (!p->c->empty() && *p->c != narrow_from_wide(cB)) return TRUE;
            if (!p->t->empty() && *p->t != narrow_from_wide(tB)) return TRUE;
            p->h = h; return FALSE;
            }, (LPARAM)&args);
        return args.h;
    }

    // ============================================================
    // 5. MAIN FUNCTIONS (Using switch/get for stability)
    // ============================================================

    export inline void save_macro()
    {
        std::ofstream out("macro.txt");
        if (!out) return;

        std::lock_guard lock(g_lock);

        // Standardized on explicit type and index-based access
        for (const Event& e : g_macro) {
            switch (e.index()) {
            case 0: { // KeyEvent (Index 0)
                const KeyEvent& ev = std::get<0>(e);
                out << "K," << ev.vk << "," << (ev.down ? 1 : 0) << "," << ev.delay << "\n";
                break;
            }
            case 1: { // MouseEvent (Index 1)
                const MouseEvent& ev = std::get<1>(e);

                std::string id = ev.windowClass;
                if (!ev.windowTitle.empty()) {
                    if (!id.empty()) id += "|";
                    id += ev.windowTitle;
                }
                out << "M," << ev.x << "," << ev.y << "," << ev.relX << "," << ev.relY
                    << "," << ev.winW << "," << ev.winH << "," << (ev.down ? 1 : 0) << "," << ev.delay;
                if (!id.empty()) out << "," << id;
                out << "\n";
                break;
            }
            }
        }
    }

    export inline bool load_macro()
    {
        std::ifstream in("macro.txt");
        if (!in) return false;
        std::vector<Event> temp;
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty()) continue;
            try {
                std::stringstream ss(line);
                std::string p[12];
                int i = 0;
                while (i < 12 && std::getline(ss, p[i], ',')) ++i;

                if (p[0] == "K" && i >= 4) {
                    temp.emplace_back(KeyEvent{ (WORD)stoi(p[1]), (p[2] == "1"), (DWORD)stoul(p[3]) });
                }
                else if (p[0] == "M" && i >= 5) {
                    MouseEvent m{};
                    if (i == 5) { // legacy
                        m.x = stoi(p[1]); m.y = stoi(p[2]); m.relX = m.x; m.relY = m.y;
                        m.down = (p[3] == "1"); m.delay = (DWORD)stoul(p[4]);
                    }
                    else { // new
                        m.x = stoi(p[1]); m.y = stoi(p[2]); m.relX = stoi(p[3]); m.relY = stoi(p[4]);
                        m.winW = stoi(p[5]); m.winH = stoi(p[6]); m.down = (p[7] == "1"); m.delay = (DWORD)stoul(p[8]);
                        if (i >= 10) {
                            size_t b = p[9].find('|');
                            if (b != std::string::npos) { m.windowClass = p[9].substr(0, b); m.windowTitle = p[9].substr(b + 1); }
                            else m.windowTitle = p[9];
                        }
                    }
                    temp.emplace_back(m);
                }
            }
            catch (...) {}
        }
        std::lock_guard lock(g_lock);
        g_macro = std::move(temp);
        return true;
    }

    export inline void playback_thread()
    {
        using namespace std::chrono_literals;
        thread_local std::mt19937 rng(std::random_device{}());

        for (;;) {
            if (g_exit.load()) return;
            if (!g_play.load()) { std::this_thread::sleep_for(5ms); continue; }

            std::vector<Event> local;
            { std::lock_guard lock(g_lock); local = g_macro; }

            consolelog::log("[PLAYBACK] Start");

            // Standardized on explicit type and index-based access
            for (const Event& ev : local) {
                if (!g_play.load() || g_exit.load()) break;

                DWORD delayVal = 0;

                switch (ev.index()) {
                case 0: { // KeyEvent (Index 0)
                    const KeyEvent& k = std::get<0>(ev);
                    sendKey(k);
                    delayVal = k.delay;
                    break;
                }
                case 1: { // MouseEvent (Index 1)
                    const MouseEvent& m = std::get<1>(ev);
                    sendMouse(m);
                    delayVal = m.delay;
                    break;
                }
                }

                std::uniform_int_distribution<int> jit(-4, 8);
                long wait = (long)delayVal + jit(rng);
                if (wait < 0) wait = 0;

                auto wake = std::chrono::steady_clock::now() + std::chrono::milliseconds(wait);
                while (std::chrono::steady_clock::now() < wake) {
                    if (!g_play.load() || g_exit.load()) break;
                    std::this_thread::sleep_for(1ms);
                }
            }

            if (!g_repeatEnabled.load()) g_play.store(false);

            if (g_injectedLeftDown.load()) {
                INPUT i{}; i.type = INPUT_MOUSE; i.mi.dwFlags = MOUSEEVENTF_LEFTUP;
                ::SendInput(1, &i, sizeof(i));
                g_injectedLeftDown.store(false);
            }
        }
    }

    // ============================================================
    // 6. CONTROL API
    // ============================================================
    export inline bool is_recording() noexcept { return g_record.load(); }
    export inline bool is_playing() noexcept { return g_play.load(); }
    export inline void set_repeat(bool e) noexcept { g_repeatEnabled.store(e); }
    export inline void request_exit() { g_exit.store(true); ::PostQuitMessage(0); }

    export inline void clear_macro() {
        { std::lock_guard l(g_lock); g_macro.clear(); }
        g_record.store(false); g_resetMouse.store(true);
        consolelog::log("[REC] cleared");
    }

    export inline void toggle_record() {
        bool n = !g_record.load(); g_record.store(n); g_resetMouse.store(true); resetTick();
        if (n) { { std::lock_guard l(g_lock); g_macro.clear(); } consolelog::log("[REC] start"); }
        else { save_macro(); consolelog::log("[REC] saved"); }
    }

    export inline void toggle_play() {
        bool n = !g_play.load(); g_play.store(n);
        if (n) { g_record.store(false); g_resetMouse.store(true); }
        consolelog::log(n ? "[PLAY] ON" : "[PLAY] OFF");
    }

    export inline void start_playback_thread() {
        static std::once_flag f; std::call_once(f, [] { std::thread(playback_thread).detach(); });
    }

} // namespace macro