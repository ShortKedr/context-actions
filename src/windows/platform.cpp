#include "windows/platform_win.h"
#include "../platform.h"
#include <algorithm>
#include <cwctype>
#include <sstream>
#include <system_error>

namespace ca {
namespace {
[[noreturn]] void fail(const std::wstring& message) { throw std::runtime_error(utf8(message)); }
}

void check(HRESULT hr) { if (FAILED(hr)) fail(systemError(static_cast<DWORD>(hr))); }
std::wstring systemError(DWORD code) {
    wchar_t* buffer = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,nullptr,code,0,reinterpret_cast<LPWSTR>(&buffer),0,nullptr);
    std::wstring result = buffer ? buffer : L"Windows error " + std::to_wstring(code);
    if (buffer) LocalFree(buffer);
    return result;
}
std::wstring wide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (!n) throw std::runtime_error("Invalid UTF-8");
    std::wstring out(n,L'\0'); MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),out.data(),n); return out;
}
std::string utf8(const std::wstring& s) {
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),nullptr,0,nullptr,nullptr);
    std::string out(n,'\0'); WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),out.data(),n,nullptr,nullptr); return out;
}
std::wstring quote(const std::wstring& arg) {
    std::wstring out = L"\"";
    size_t slashes = 0;
    for (auto c : arg) {
        if (c == L'\\') { ++slashes; continue; }
        out.append(slashes * (c == L'"' ? 2 : 1), L'\\'); slashes = 0;
        if (c == L'"') out += L'\\';
        out += c;
    }
    out.append(slashes*2,L'\\'); return out + L'"';
}
fs::path appDirectory() {
    wchar_t path[32768];
    if (!GetModuleFileNameW(nullptr,path,32768)) fail(systemError(GetLastError()));
    return fs::path(path).parent_path();
}
void launch(const fs::path& executable, const std::vector<std::wstring>& args) {
    std::wstring line=quote(executable.wstring()); for (const auto& arg:args) line+=L" "+quote(arg);
    if (line.size()>=32767) fail(tr("invalid"));
    STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION process{};
    if (!CreateProcessW(executable.c_str(),line.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,executable.parent_path().c_str(),&startup,&process)) fail(systemError(GetLastError()));
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
}
ProcessResult runProcess(const fs::path& exe, const std::vector<std::wstring>& args, std::atomic_bool& cancel,
    const std::function<void(const std::string&)>& onOutput, uint32_t timeoutMs) {
    if (cancel) throw Cancelled();
    std::wstring line=quote(exe.wstring()); for (const auto& arg:args) line+=L" "+quote(arg);
    if (line.size()>=32767) fail(tr("invalid"));
    SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE}; HANDLE readRaw=nullptr,writeRaw=nullptr;
    if (!CreatePipe(&readRaw,&writeRaw,&security,0)) fail(systemError(GetLastError()));
    Handle reader(readRaw),writer(writeRaw),nullInput(CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&security,OPEN_EXISTING,0,nullptr));
    SetHandleInformation(reader.value,HANDLE_FLAG_INHERIT,0);
    Handle job(CreateJobObjectW(nullptr,nullptr));
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{}; limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!job.value || !SetInformationJobObject(job.value,JobObjectExtendedLimitInformation,&limits,sizeof(limits))) fail(systemError(GetLastError()));
    STARTUPINFOW startup{sizeof(startup)}; startup.dwFlags=STARTF_USESTDHANDLES;startup.hStdInput=nullInput.value;startup.hStdOutput=writer.value;startup.hStdError=writer.value;
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(exe.c_str(),line.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW|CREATE_SUSPENDED,nullptr,exe.parent_path().c_str(),&startup,&process)) fail(systemError(GetLastError()));
    Handle child(process.hProcess),thread(process.hThread);
    if (!AssignProcessToJobObject(job.value,child.value)) { const auto error=GetLastError(); TerminateProcess(child.value,1); fail(systemError(error)); }
    ResumeThread(thread.value); CloseHandle(writer.value);writer.value=nullptr;
    std::string output; const auto start=GetTickCount64();
    for (;;) {
        DWORD available=0;
        while (PeekNamedPipe(reader.value,nullptr,0,nullptr,&available,nullptr) && available) {
            char data[4096];DWORD count=0;
            if (!ReadFile(reader.value,data,std::min<DWORD>(sizeof(data),available),&count,nullptr) || !count) break;
            std::string chunk(data,count); if(onOutput) onOutput(chunk);
            output+=chunk; if(output.size()>65536) output.erase(0,output.size()-65536);
        }
        if (WaitForSingleObject(child.value,20)==WAIT_OBJECT_0) {
            // Drain bytes written between the previous pipe read and process exit.
            DWORD availableAfter=0; if (PeekNamedPipe(reader.value,nullptr,0,nullptr,&availableAfter,nullptr) && availableAfter) continue;
            break;
        }
        if (cancel || (timeoutMs!=INFINITE && GetTickCount64()-start>timeoutMs)) {
            TerminateJobObject(job.value,ERROR_CANCELLED); WaitForSingleObject(child.value,5000);
            if (cancel) throw Cancelled(); fail(L"Operation timed out.");
        }
    }
    if (cancel) throw Cancelled();
    DWORD code=1; GetExitCodeProcess(child.value,&code); return {static_cast<uint32_t>(code),output};
}
fs::path dataDirectory() {
    wchar_t value[32768]{};
    if (GetEnvironmentVariableW(L"CONTEXT_ACTIONS_DATA_DIR", value, 32768)) return fs::path(value);
    if (!GetEnvironmentVariableW(L"LOCALAPPDATA", value, 32768)) throw std::runtime_error("LOCALAPPDATA is unavailable");
    return fs::path(value) / L"Context Actions";
}

void createPrivateDirectory(const fs::path& path) {
    if (!fs::create_directory(path)) fail(tr("conflict") + pathText(path));
}

void ensurePrivateDirectory(const fs::path& path) { fs::create_directories(path); }

File openNewPrivateFile(const fs::path& path) {
    std::FILE* file = nullptr;
    if (const auto error = _wfopen_s(&file, path.c_str(), L"wbxN"))
        throw fs::filesystem_error("Create new file", path, std::error_code(error, std::generic_category()));
    return File(file);
}

fs::path engine(const char* name) {
    const auto filename = wide(name) + L".exe";
    const auto bundled = appDirectory() / L"engines" / filename;
    if (fs::is_regular_file(bundled)) return bundled;

    wchar_t searchPath[32768]{};
    const auto length = GetEnvironmentVariableW(L"PATH", searchPath, std::size(searchPath));
    if (length && length < std::size(searchPath)) {
        std::wistringstream directories(searchPath);
        std::wstring directory;
        while (std::getline(directories, directory, L';')) {
            if (directory.size() >= 2 && directory.front() == L'"' && directory.back() == L'"')
                directory = directory.substr(1, directory.size() - 2);
            const fs::path path(directory);
            std::error_code error;
            if (path.is_absolute() && fs::is_regular_file(path / filename, error)) return path / filename;
        }
    }
    fail(tr("systemEngine") + filename);
}
std::wstring readLanguageSetting() {
    wchar_t value[LOCALE_NAME_MAX_LENGTH]{};
    GetPrivateProfileStringW(L"ui", L"language", L"", value, std::size(value), (dataDirectory() / L"settings.ini").c_str());
    if (!*value) GetUserDefaultLocaleName(value, std::size(value));
    return value;
}
void writeLanguageSetting(const std::wstring& value) {
    if (!WritePrivateProfileStringW(L"ui", L"language", value.c_str(), (dataDirectory() / L"settings.ini").c_str()))
        fail(systemError(GetLastError()));
}
std::wstring pathText(const fs::path& path) { return path.native(); }
fs::path textPath(const std::wstring& value) { return fs::path(value); }
std::wstring pathKey(const fs::path& path) {
    auto key = pathText(path);
    for (auto& c : key) c = towlower(c);
    return key;
}
void moveFile(const fs::path& from, const fs::path& to, bool replace) {
    if (!MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_WRITE_THROUGH | (replace ? MOVEFILE_REPLACE_EXISTING : 0))) {
        const auto error = GetLastError();
        fail(pathText(from) + L" → " + pathText(to) + L"\n" + systemError(error));
    }
}
void withRenameLock(const fs::path& file, const std::function<void()>& operation) {
    const Handle lock(CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (lock.value == INVALID_HANDLE_VALUE) {
        const auto error = GetLastError();
        if (error == ERROR_SHARING_VIOLATION) fail(tr("renameBusy"));
        fail(systemError(error));
    }
    operation();
}
}
