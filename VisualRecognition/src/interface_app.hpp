
#pragma once

// Thin compatibility header in case non-modular translation units
// want to call RunUI. In modular builds, simply `import interface.app;`.

#include <windows.h>

void RunUI(HINSTANCE instance, int cmdShow);
