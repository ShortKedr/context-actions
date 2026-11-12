#pragma once
#include <atomic>
#include <cstdint>
#include <optional>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>
#include <stdexcept>

namespace ca {
namespace fs = std::filesystem;
enum class Kind { other, video, image, pdf, document, sheet, audio, archive };
enum class Action { more, compress, h264, h265, telegram, image, pdf, rename, word, excel, documents, sheets, audio, remux, frame, mediaInfo, repack };
enum class Capability { unknown, available, unavailable };
struct Options {
    Action action = Action::more;
    std::vector<fs::path> files;
    fs::path output;
    int quality = 24, width = 0, fps = 0, targetMB = 0;
    bool mute = false, firstFrame = false;
    std::wstring format, pattern = L"{name}_{n}", prefix, suffix, find, replace;
    int start = 1;
};
struct Update { size_t index; double progress; std::wstring status; fs::path output; bool failed = false; };
using Progress = std::function<void(const Update&)>;
struct Result { int succeeded = 0, failed = 0; bool cancelled = false; std::vector<fs::path> outputs; };
struct Rename { fs::path from, to; };
struct ProcessResult { uint32_t code; std::string output; };
struct Cancelled : std::runtime_error { Cancelled() : std::runtime_error("Cancelled") {} };

struct CommandLine {
    Options options;
    fs::path report;
    bool headless = false, immediate = false, help = false;
    bool chooseAction = false;
};
void parseArguments(const std::vector<std::wstring>& arguments, CommandLine& command);
std::optional<int> runCommandLine(const CommandLine& command);
void reportCommandError(const CommandLine& command, const std::exception& error);

std::wstring wide(const std::string& s);
std::string utf8(const std::wstring& s);
std::wstring pathText(const fs::path& path);
fs::path textPath(const std::wstring& value);
fs::path appDirectory();
fs::path dataDirectory();
std::wstring language();
void setLanguage(const std::wstring& value);
std::wstring tr(const char* key);
Kind kind(const fs::path& path);
std::wstring actionId(Action action);
Action parseAction(const std::wstring& value);
const char* actionKey(Action action);
std::wstring trAction(Action action);
std::vector<Action> catalog();
struct ActionChoice {
    Action action;
    std::wstring label;
    std::wstring unavailable;
};
std::vector<ActionChoice> actionChoices(const std::vector<fs::path>& files);
std::vector<std::wstring> outputFormats(Action action);
std::vector<std::wstring> outputFormats(Action action, const std::vector<fs::path>& files);
std::wstring outputFormat(const Options& options);
bool accepts(Action action, const std::vector<fs::path>& files);
std::wstring unavailableReason(Action action, const std::vector<fs::path>& files);
void validate(const Options& options);
fs::path writeJob(const Options& options);
Options readJob(const fs::path& path);
void consumeJob(const fs::path& path);
void saveOptions(const Options& options, const fs::path& path);
std::vector<std::wstring> videoArguments(const Options& options, const fs::path& input, const fs::path& output, double duration = 0, int attempt = 0);
ProcessResult runProcess(const fs::path& executable, const std::vector<std::wstring>& arguments,
    std::atomic_bool& cancel, const std::function<void(const std::string&)>& onOutput = {},
    uint32_t timeoutMs = UINT32_MAX, size_t processMemoryLimit = 0);
std::vector<Rename> previewRename(const Options& options);
void applyRename(const std::vector<Rename>& changes, std::atomic_bool& cancel, bool saveUndo = true);
void undoRename(std::atomic_bool& cancel);
Result execute(const Options& options, std::atomic_bool& cancel, const Progress& progress);
fs::path uniqueOutput(const fs::path& directory, const std::wstring& stem, const std::wstring& extension);
void commitOutput(const fs::path& temporary, const fs::path& destination);
Capability officeCapability(bool excel);
void setOfficeCapability(bool excel, Capability state);
void resetOfficeCapabilities();
void exportOfficePdf(const fs::path& input, const fs::path& output, bool excel, std::atomic_bool& cancel);
}
