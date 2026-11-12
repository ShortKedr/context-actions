#include "platform_win.h"
#include <shellapi.h>

int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int) {
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32|LOAD_LIBRARY_SEARCH_APPLICATION_DIR);
    const auto apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    struct Apartment {
        HRESULT result;
        ~Apartment() { if (SUCCEEDED(result)) CoUninitialize(); }
    } com{apartment};
    ca::CommandLine command;
    try {
        ca::check(apartment);
        int argc = 0;
        auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (!argv) ca::check(HRESULT_FROM_WIN32(GetLastError()));
        struct Args { LPWSTR* value; ~Args() { LocalFree(value); } } owner{argv};
        ca::parseArguments(std::vector<std::wstring>(argv + 1, argv + argc), command);
        if (const auto status = ca::runCommandLine(command)) return *status;
        return ca::runUi(std::move(command.options), command.immediate, command.chooseAction);
    } catch (const std::exception& e) {
        if (command.headless) ca::reportCommandError(command, e);
        else MessageBoxW(nullptr, ca::wide(e.what()).c_str(), L"Context Actions", MB_OK|MB_ICONERROR);
        return 1;
    }
}
