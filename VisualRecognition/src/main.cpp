#include "ui.hpp"

#include <windows.h>

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int cmdShow)
{
    RunUI(hInstance, cmdShow);
    return 0;
}
