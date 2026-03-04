#include "util.h"

namespace util {

bool setup_console(const char* title)
{
    if (!AllocConsole())
        return false;

    FILE* fp = nullptr;
    freopen_s(&fp, "CONIN$", "r", stdin);
    freopen_s(&fp, "CONOUT$", "w", stdout);
    freopen_s(&fp, "CONOUT$", "w", stderr);

    SetConsoleTitleA(title);
    return true;
}

} // namespace util
