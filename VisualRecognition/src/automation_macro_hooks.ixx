module;
#define NOMINMAX
#include <windows.h>

export module automation.macro.hooks;

// Forward declarations only. Implementations live in automation.macro.engine.
export namespace macro
{
    LRESULT CALLBACK mouse_proc(int code, WPARAM wp, LPARAM lp);
    LRESULT CALLBACK key_proc(int code, WPARAM wp, LPARAM lp);

    bool install_hooks(HINSTANCE hInstance);
    void uninstall_hooks();
}
