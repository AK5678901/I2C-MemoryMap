#include "application.hpp"

#define NOMINMAX
#include <windows.h>

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    Application application;
    return application.run();
}
