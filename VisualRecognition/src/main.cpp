
    #include <windows.h>
    import ui;

    void RunUI(HINSTANCE instance, int cmdShow);

    int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int cmdShow)
    {
        RunUI(instance, cmdShow);
        return 0;
    }
