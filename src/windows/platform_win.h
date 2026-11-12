#pragma once
#include "core.h"
#include <windows.h>
#include <wrl/client.h>

namespace ca {
using Microsoft::WRL::ComPtr;

struct Handle {
    HANDLE value = nullptr;
    explicit Handle(HANDLE handle = nullptr) : value(handle) {}
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};

void check(HRESULT hr);
std::wstring systemError(DWORD code);
std::wstring quote(const std::wstring& argument);
void launch(const fs::path& executable, const std::vector<std::wstring>& arguments);
int runUi(Options options, bool immediate, bool chooseAction);
}
