#pragma once
#include <windows.h>
#include <wrl/client.h>
#include <atomic>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>
#include <stdexcept>

namespace ca {
namespace fs = std::filesystem;
using Microsoft::WRL::ComPtr;
enum class Kind { other, video, image, pdf, document, sheet };
enum class Action { more, compress, h264, h265, telegram, image, pdf, rename, word, excel, documents, sheets };
enum class Capability { unknown, available, unavailable };
struct Options {
    Action action = Action::more;
    std::vector<fs::path> files;
    fs::path output;
    int quality = 24, width = 0, fps = 0, targetMB = 0;
    bool mute = false, firstFrame = false;
    std::wstring format = L"png", pattern = L"{name}_{n}", prefix, suffix, find, replace;
    int start = 1;
};
struct Update { size_t index; double progress; std::wstring status; fs::path output; };
using Progress = std::function<void(const Update&)>;
struct Result { int succeeded = 0, failed = 0; bool cancelled = false; std::vector<fs::path> outputs; };
struct Rename { fs::path from, to; };
struct ProcessResult { DWORD code; std::string output; };
struct Handle {
    HANDLE value = nullptr;
    explicit Handle(HANDLE h = nullptr) : value(h) {}
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};
struct Cancelled : std::runtime_error { Cancelled() : std::runtime_error("Cancelled") {} };
void check(HRESULT hr);
std::wstring wide(const std::string& s);
std::string utf8(const std::wstring& s);
std::wstring systemError(DWORD code);
std::wstring quote(const std::wstring& argument);
fs::path appDirectory();
fs::path dataDirectory();
std::wstring language();
void setLanguage(const std::wstring& value);
std::wstring tr(const char* key);
Kind kind(const fs::path& path);
std::wstring actionId(Action action);
Action parseAction(const std::wstring& value);
const char* actionKey(Action action);
std::vector<Action> catalog();
bool accepts(Action action, const std::vector<fs::path>& files);
std::wstring unavailableReason(Action action, const std::vector<fs::path>& files);
void validate(const Options& options);
fs::path writeJob(const Options& options);
Options readJob(const fs::path& path);
void consumeJob(const fs::path& path);
void saveOptions(const Options& options, const fs::path& path);
std::vector<std::wstring> videoArguments(const Options& options, const fs::path& input, const fs::path& output, double duration = 0, int attempt = 0);
ProcessResult runProcess(const fs::path& executable, const std::vector<std::wstring>& arguments,
    std::atomic_bool& cancel, const std::function<void(const std::string&)>& onOutput = {}, DWORD timeoutMs = INFINITE);
void launch(const fs::path& executable, const std::vector<std::wstring>& arguments);
std::vector<Rename> previewRename(const Options& options);
void applyRename(const std::vector<Rename>& changes, std::atomic_bool& cancel, bool saveUndo = true);
void undoRename(std::atomic_bool& cancel);
Result execute(const Options& options, std::atomic_bool& cancel, const Progress& progress);
fs::path uniqueOutput(const fs::path& directory, const std::wstring& stem, const std::wstring& extension);
void commitOutput(const fs::path& temporary, const fs::path& destination);
void createImagePdf(const fs::path& image, const fs::path& output);
Capability officeCapability(bool excel);
void setOfficeCapability(bool excel, Capability state);
void resetOfficeCapabilities();
void exportOfficePdf(const fs::path& input, const fs::path& output, bool excel, std::atomic_bool& cancel);
int runUi(Options options, bool immediate, bool smoke = false);
}
