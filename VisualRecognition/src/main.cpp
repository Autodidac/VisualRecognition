
#include <windows.h>
import interface.app;

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int cmdShow)
{
    RunUI(instance, cmdShow);
    return 0;
}
