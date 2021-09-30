#include "util.h"

namespace UTIL
{
  void setup_console(const char* name)
  {
    AllocConsole();
    freopen_s( (FILE**)stdin, "CONIN$", "r", stdin );
    freopen_s( (FILE**)stdout, "CONOUT$", "w", stdout );
    freopen_s( (FILE**)stderr, "CONOUT$", "w", stderr );
    SetConsoleTitleA( name );
  }
}
