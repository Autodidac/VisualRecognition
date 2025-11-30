module;
#define NOMINMAX
#include <windows.h>

export module vr.macro.types;

import std;
import <algorithm>;

import vr.console_log;

namespace macro
{
    // ------------------------------------------------------------
    // Types
    // ------------------------------------------------------------

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
        int x{}, y{};          // recorded absolute screen coords (fallback)
        bool down{};
        DWORD delay{};

        std::string windowClass;
        std::string windowTitle;

        int relX{}, relY{};
        int winW{}, winH{};
    };

    export using Event = std::variant<KeyEvent, MouseEvent>;
    export using Clock = std::chrono::steady_clock;

    // ------------------------------------------------------------
    // Globals
    // ------------------------------------------------------------

    export inline std::vector<Event>      g_macro{};
    export inline std::mutex              g_lock{};
    export inline std::atomic<bool>       g_record{ false };
    export inline std::atomic<bool>       g_play{ false };
    export inline std::atomic<bool>       g_exit{ false };
    export inline std::atomic<bool>       g_resetMouse{ false };
    export inline std::atomic<long long>  g_lastTick{ 0 };
    export inline std::atomic<bool>       g_repeatEnabled{ false };
    export inline std::atomic<bool>       g_injectedLeftDown{ false };

    // ------------------------------------------------------------
    // Time helpers
    // ------------------------------------------------------------

    export inline long long nowMs() noexcept
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            Clock::now().time_since_epoch()).count();
    }

    export inline void resetTick() noexcept
    {
        g_lastTick.store(nowMs());
    }

    // ------------------------------------------------------------
    // UTF-8 helpers
    // ------------------------------------------------------------

    export inline std::string narrow_from_wide(const std::wstring& ws)
    {
        if (ws.empty()) return {};
        const int len = ::WideCharToMultiByte(
            CP_UTF8, 0,
            ws.data(), static_cast<int>(ws.size()),
            nullptr, 0,
            nullptr, nullptr);
        if (len <= 0) return {};
        std::string out(static_cast<std::size_t>(len), '\0');
        ::WideCharToMultiByte(
            CP_UTF8, 0,
            ws.data(), static_cast<int>(ws.size()),
            out.data(), len,
            nullptr, nullptr);
        return out;
    }

    export inline std::string narrow_from_wide(const wchar_t* w)
    {
        if (!w || !*w) return {};
        const int lenW = ::lstrlenW(w);
        if (lenW <= 0) return {};
        const int len = ::WideCharToMultiByte(
            CP_UTF8, 0,
            w, lenW,
            nullptr, 0,
            nullptr, nullptr);
        if (len <= 0) return {};
        std::string out(static_cast<std::size_t>(len), '\0');
        ::WideCharToMultiByte(
            CP_UTF8, 0,
            w, lenW,
            out.data(), len,
            nullptr, nullptr);
        return out;
    }

    // ------------------------------------------------------------
    // Window helpers for mouse events
    // ------------------------------------------------------------

    export inline void capture_window_info(const POINT& pt, MouseEvent& ev)
    {
        HWND h = ::WindowFromPoint(pt);
        if (!h)
        {
            ev.relX = ev.x;
            ev.relY = ev.y;
            ev.winW = 0;
            ev.winH = 0;
            return;
        }

        HWND root = ::GetAncestor(h, GA_ROOT);
        if (!root) root = h;

        RECT wr{};
        if (::GetWindowRect(root, &wr))
        {
            ev.relX = pt.x - wr.left;
            ev.relY = pt.y - wr.top;
            ev.winW = wr.right - wr.left;
            ev.winH = wr.bottom - wr.top;
        }
        else
        {
            ev.relX = ev.x;
            ev.relY = ev.y;
            ev.winW = 0;
            ev.winH = 0;
        }

        wchar_t classBuf[256]{};
        wchar_t titleBuf[256]{};
        ::GetClassNameW(root, classBuf, 255);
        ::GetWindowTextW(root, titleBuf, 255);

        ev.windowClass = narrow_from_wide(classBuf);
        ev.windowTitle = narrow_from_wide(titleBuf);
    }

    export inline HWND find_window_for_mouse(const MouseEvent& ev)
    {
        if (ev.windowClass.empty() && ev.windowTitle.empty())
            return nullptr;

        struct Payload
        {
            const std::string* cls{};
            const std::string* title{};
            HWND               hwnd{};
        } payload{ &ev.windowClass, &ev.windowTitle, nullptr };

        ::EnumWindows(
            [](HWND h, LPARAM l) -> BOOL
            {
                auto* p = reinterpret_cast<Payload*>(l);
                if (p->hwnd) return FALSE;

                wchar_t classBuf[256]{};
                wchar_t titleBuf[256]{};
                ::GetClassNameW(h, classBuf, 255);
                ::GetWindowTextW(h, titleBuf, 255);

                std::string cls = narrow_from_wide(classBuf);
                std::string ttl = narrow_from_wide(titleBuf);

                if (!p->cls->empty() && *p->cls != cls)
                    return TRUE;
                if (!p->title->empty() && *p->title != ttl)
                    return TRUE;

                p->hwnd = h;
                return FALSE;
            },
            reinterpret_cast<LPARAM>(&payload));

        return payload.hwnd;
    }

    // ------------------------------------------------------------
    // Save / Load
    // ------------------------------------------------------------

    export inline void save_macro()
    {
        std::ofstream out("macro.txt");
        if (!out) return;

        std::lock_guard lock(g_lock);
        for (auto& e : g_macro)
        {
            std::visit(
                [&](auto& ev)
                {
                    using T = std::decay_t<decltype(ev)>;

                    if constexpr (std::is_same_v<T, KeyEvent>)
                    {
                        out << "K," << ev.vk << "," << (ev.down ? 1 : 0)
                            << "," << ev.delay << "\n";
                    }
                    else
                    {
                        std::string id;
                        if (!ev.windowClass.empty() || !ev.windowTitle.empty())
                        {
                            id = ev.windowClass;
                            id.push_back('|');
                            id += ev.windowTitle;
                        }

                        out << "M,"
                            << ev.x << ","
                            << ev.y << ","
                            << ev.relX << ","
                            << ev.relY << ","
                            << ev.winW << ","
                            << ev.winH << ","
                            << (ev.down ? 1 : 0) << ","
                            << ev.delay;

                        if (!id.empty())
                            out << "," << id;

                        out << "\n";
                    }
                },
                e);
        }
    }

    export inline bool load_macro()
    {
        std::ifstream in("macro.txt");
        if (!in) return false;

        std::vector<Event> temp;
        std::string line;

        while (std::getline(in, line))
        {
            if (line.empty()) continue;

            std::stringstream ss(line);
            std::string p[12];
            int idx = 0;
            while (idx < 12 && std::getline(ss, p[idx], ',')) ++idx;
            if (idx == 0) continue;

            if (p[0] == "K" && idx >= 4)
            {
                KeyEvent kev{
                    static_cast<WORD>(std::stoi(p[1])),
                    (p[2] == "1"),
                    static_cast<DWORD>(std::stoul(p[3]))
                };
                temp.emplace_back(kev);
            }
            else if (p[0] == "M")
            {
                MouseEvent mev{};

                if (idx == 5)
                {
                    mev.x = std::stoi(p[1]);
                    mev.y = std::stoi(p[2]);
                    mev.relX = mev.x;
                    mev.relY = mev.y;
                    mev.winW = 0;
                    mev.winH = 0;
                    mev.down = (p[3] == "1");
                    mev.delay = static_cast<DWORD>(std::stoul(p[4]));
                }
                else if (idx >= 9)
                {
                    mev.x = std::stoi(p[1]);
                    mev.y = std::stoi(p[2]);
                    mev.relX = std::stoi(p[3]);
                    mev.relY = std::stoi(p[4]);
                    mev.winW = std::stoi(p[5]);
                    mev.winH = std::stoi(p[6]);
                    mev.down = (p[7] == "1");
                    mev.delay = static_cast<DWORD>(std::stoul(p[8]));

                    if (idx >= 10)
                    {
                        auto& id = p[9];
                        const auto barPos = id.find('|');
                        if (barPos != std::string::npos)
                        {
                            mev.windowClass = id.substr(0, barPos);
                            mev.windowTitle = id.substr(barPos + 1);
                        }
                        else
                        {
                            mev.windowTitle = id;
                        }
                    }
                }
                else
                {
                    continue;
                }

                temp.emplace_back(mev);
            }
        }

        std::lock_guard lock(g_lock);
        g_macro = std::move(temp);
        return true;
    }

    // ------------------------------------------------------------
    // Input send
    // ------------------------------------------------------------

    inline bool is_extended_vk(WORD vk) noexcept
    {
        switch (vk)
        {
        case VK_INSERT: case VK_DELETE:
        case VK_HOME:   case VK_END:
        case VK_PRIOR:  case VK_NEXT:
        case VK_LEFT:   case VK_RIGHT:
        case VK_UP:     case VK_DOWN:
        case VK_RCONTROL:
        case VK_RMENU:
        case VK_LWIN:   case VK_RWIN:
        case VK_APPS:
            return true;
        default:
            return false;
        }
    }

    export inline void sendKey(const KeyEvent& e)
    {
        INPUT in{};
        in.type = INPUT_KEYBOARD;
        in.ki.wVk = 0;
        in.ki.wScan = static_cast<WORD>(::MapVirtualKeyW(e.vk, MAPVK_VK_TO_VSC));
        in.ki.dwFlags = KEYEVENTF_SCANCODE | (e.down ? 0 : KEYEVENTF_KEYUP);

        if (is_extended_vk(e.vk))
        {
            in.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
        }

        ::SendInput(1, &in, sizeof(in));
    }

    export inline void sendMouse(const MouseEvent& e)
    {
        // 1. Resolve target window + rect
        HWND target = nullptr;

        if (!e.windowClass.empty() || !e.windowTitle.empty())
            target = find_window_for_mouse(e);

        if (!target)
            target = ::GetForegroundWindow();

        RECT wr{};
        if (target && !::GetWindowRect(target, &wr))
            target = nullptr;

        // 2. Determine monitor rect
        RECT mr{};
        if (target)
        {
            HMONITOR mon = ::MonitorFromWindow(target, MONITOR_DEFAULTTONEAREST);
            MONITORINFOEXW mi{};
            mi.cbSize = sizeof(mi);
            if (::GetMonitorInfoW(mon, &mi))
            {
                mr = mi.rcMonitor;
            }
            else
            {
                mr.left = ::GetSystemMetrics(SM_XVIRTUALSCREEN);
                mr.top = ::GetSystemMetrics(SM_YVIRTUALSCREEN);
                mr.right = mr.left + ::GetSystemMetrics(SM_CXVIRTUALSCREEN);
                mr.bottom = mr.top + ::GetSystemMetrics(SM_CYVIRTUALSCREEN);
            }
        }
        else
        {
            mr.left = ::GetSystemMetrics(SM_XVIRTUALSCREEN);
            mr.top = ::GetSystemMetrics(SM_YVIRTUALSCREEN);
            mr.right = mr.left + ::GetSystemMetrics(SM_CXVIRTUALSCREEN);
            mr.bottom = mr.top + ::GetSystemMetrics(SM_CYVIRTUALSCREEN);
        }

        // 3. Compute target point (window-relative + scaling)
        POINT targetPt{ e.x, e.y }; // fallback

        if (target)
        {
            const int curW = wr.right - wr.left;
            const int curH = wr.bottom - wr.top;

            int relX = e.relX;
            int relY = e.relY;

            if (e.winW > 0 && e.winH > 0 && curW > 0 && curH > 0)
            {
                const double sx = static_cast<double>(curW) / static_cast<double>(e.winW);
                const double sy = static_cast<double>(curH) / static_cast<double>(e.winH);
                relX = static_cast<int>(std::lround(e.relX * sx));
                relY = static_cast<int>(std::lround(e.relY * sy));
            }

            targetPt.x = wr.left + relX;
            targetPt.y = wr.top + relY;
        }

        // Clamp inside monitor
        targetPt.x = std::clamp(targetPt.x, mr.left, mr.right - 1);
        targetPt.y = std::clamp(targetPt.y, mr.top, mr.bottom - 1);

        // 4. Bezier movement
        POINT startPt{};
        if (!::GetCursorPos(&startPt))
            startPt = targetPt;

        const int dx0 = targetPt.x - startPt.x;
        const int dy0 = targetPt.y - startPt.y;

        DWORD baseDuration = e.delay;
        if (baseDuration < 80)  baseDuration = 80;
        if (baseDuration > 350) baseDuration = 350;
        if (e.delay == 0)       baseDuration = 140;

        const DWORD stepMs = 5;
        unsigned steps = static_cast<unsigned>(baseDuration / stepMs);
        if (steps < 8)   steps = 8;
        if (steps > 120) steps = 120;

        auto make_ctrl_offset = [](int dx, int dy)
            {
                double length = std::sqrt(static_cast<double>(dx * dx + dy * dy));
                if (length < 1.0) length = 1.0;
                const double nx = -static_cast<double>(dy) / length;
                const double ny = static_cast<double>(dx) / length;
                const double magnitude = (std::min)(length * 0.15, 60.0);
                return std::pair<double, double>(nx * magnitude, ny * magnitude);
            };

        POINT  p0 = startPt;
        POINT  p3 = targetPt;
        auto [ox, oy] = make_ctrl_offset(dx0, dy0);

        POINTF p1{
            static_cast<float>(p0.x + dx0 * 0.3 + ox),
            static_cast<float>(p0.y + dy0 * 0.3 + oy)
        };
        POINTF p2{
            static_cast<float>(p0.x + dx0 * 0.7 - ox),
            static_cast<float>(p0.y + dy0 * 0.7 - oy)
        };

        auto bezier = [](double t, double p0, double p1, double p2, double p3)
            {
                const double u = 1.0 - t;
                const double tt = t * t;
                const double uu = u * u;
                const double uuu = uu * u;
                const double ttt = tt * t;
                return uuu * p0 +
                    3.0 * uu * t * p1 +
                    3.0 * u * tt * p2 +
                    ttt * p3;
            };

        static thread_local std::mt19937 rng{ std::random_device{}() };
        std::uniform_int_distribution<int> jitterPx(-1, 1);

        if (dx0 != 0 || dy0 != 0)
        {
            for (unsigned i = 1; i <= steps; ++i)
            {
                const double t = static_cast<double>(i) / static_cast<double>(steps);
                const double tEase = t * t * (3.0 - 2.0 * t); // smoothstep

                const double bx = bezier(tEase, p0.x, p1.x, p2.x, p3.x);
                const double by = bezier(tEase, p0.y, p1.y, p2.y, p3.y);

                int x = static_cast<int>(std::lround(bx)) + jitterPx(rng);
                int y = static_cast<int>(std::lround(by)) + jitterPx(rng);

                x = std::clamp(x, static_cast<int>(mr.left), static_cast<int>(mr.right - 1));
                y = std::clamp(y, static_cast<int>(mr.top), static_cast<int>(mr.bottom - 1));

                INPUT in{};
                in.type = INPUT_MOUSE;
                in.mi.dx = (x - mr.left) * 65535 / (mr.right - mr.left);
                in.mi.dy = (y - mr.top) * 65535 / (mr.bottom - mr.top);
                in.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;

                ::SendInput(1, &in, sizeof(in));
            }
        }

        // 6. Click
        INPUT click{};
        click.type = INPUT_MOUSE;
        click.mi.dwFlags = e.down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
        ::SendInput(1, &click, sizeof(click));

        if (e.down)
            g_injectedLeftDown.store(true);
        else
            g_injectedLeftDown.store(false);
    }

    // ------------------------------------------------------------
    // Playback thread
    // ------------------------------------------------------------

    export inline void playback_thread()
    {
        using namespace std::chrono_literals;
        thread_local std::mt19937 rng(std::random_device{}());

        for (;;)
        {
            if (g_exit.load())
                return;

            if (!g_play.load())
            {
                std::this_thread::sleep_for(5ms);
                continue;
            }

            std::vector<Event> local;
            {
                std::lock_guard lock(g_lock);
                local = g_macro;
            }

            {
                std::ostringstream oss;
                oss << "[PLAYBACK] " << local.size() << " events";
                consolelog::log(oss.str());
            }

            for (auto& ev : local)        // ev is the std::variant<Event>
            {
                if (!g_play.load() || g_exit.load())
                    break;

                DWORD baseDelay = 0;

                std::visit(
                    [&](auto& alt) -> void      // force void return
                    {
                        using T = std::decay_t<decltype(alt)>;

                        if constexpr (std::is_same_v<T, KeyEvent>)
                        {
                            macro::sendKey(alt);
                            baseDelay = alt.delay;
                        }
                        else
                        {
                            macro::sendMouse(alt);
                            baseDelay = alt.delay;
                        }
                    },
                    ev
                );


                std::uniform_int_distribution<int> jitter(-4, 8);
                long finalDelay = static_cast<long>(baseDelay) + jitter(rng);
                if (finalDelay < 0) finalDelay = 0;

                std::this_thread::sleep_for(std::chrono::milliseconds(finalDelay));
            }

            if (!g_repeatEnabled.load())
            {
                g_play.store(false);
            }

            if (g_injectedLeftDown.load())
            {
                INPUT in{};
                in.type = INPUT_MOUSE;
                in.mi.dwFlags = MOUSEEVENTF_LEFTUP;
                ::SendInput(1, &in, sizeof(in));
                g_injectedLeftDown.store(false);
                consolelog::log("[PLAYBACK] Forced mouse LEFTUP at end of macro");
            }
        }
    }

    // ------------------------------------------------------------
    // High-level control API for UI + hooks
    // ------------------------------------------------------------

    export inline bool is_recording() noexcept
    {
        return g_record.load();
    }

    export inline bool is_playing() noexcept
    {
        return g_play.load();
    }

    export inline void set_repeat(bool enabled) noexcept
    {
        g_repeatEnabled.store(enabled);
    }

    export inline void request_exit()
    {
        g_exit.store(true);
        ::PostQuitMessage(0);
        consolelog::log("[EXIT]");
    }

    export inline void clear_macro()
    {
        {
            std::lock_guard lock(g_lock);
            g_macro.clear();
        }
        g_record.store(false);
        g_resetMouse.store(true);
        consolelog::log("[REC] cleared");
    }

    export inline void toggle_record()
    {
        const bool newState = !g_record.load();
        g_record.store(newState);
        g_resetMouse.store(true);
        resetTick();

        if (newState)
        {
            std::lock_guard lock(g_lock);
            g_macro.clear();
            consolelog::log("[REC] start (cleared previous macro)");
        }
        else
        {
            save_macro();
            consolelog::log("[REC] stop + save");
        }
    }

    export inline void toggle_play()
    {
        const bool newPlay = !g_play.load();
        g_play.store(newPlay);
        if (newPlay)
        {
            g_record.store(false);
            g_resetMouse.store(true);
        }

        const char* state = newPlay ? "ON" : "OFF";
        std::string msg = std::string("[PLAY] ") + state;
        consolelog::log(msg);
    }

    // ------------------------------------------------------------
    // Playback thread bootstrap
    // ------------------------------------------------------------

    export inline void start_playback_thread()
    {
        static std::once_flag once;
        std::call_once(once, []
            {
                std::thread(playback_thread).detach();
            });
    }

} // namespace macro
