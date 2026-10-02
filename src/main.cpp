#include "engine/App.h"

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    return (int)lp::App::Run(GetCommandLineW());
}
