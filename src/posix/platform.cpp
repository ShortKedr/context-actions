#include "platform.h"

#include <cerrno>
#include <chrono>
#include <codecvt>
#include <cstdio>
#include <cstdlib>
#include <cwctype>
#include <fstream>
#include <locale>
#include <sstream>
#include <string_view>
#include <system_error>

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <crt_externs.h>
#include <mach-o/dyld.h>
#else
#include <linux/fs.h>
#include <sys/syscall.h>
extern char** environ;
#endif

namespace ca {
namespace {
void require(int error, const char* operation) {
    if (error) throw std::system_error(error, std::generic_category(), operation);
}

struct Descriptor {
    int value;
    ~Descriptor() { close(); }
    void close() {
        if (value != -1) ::close(value);
        value = -1;
    }
    void closeOnExec() {
        if (value <= STDERR_FILENO) {
            const int copy = fcntl(value, F_DUPFD_CLOEXEC, STDERR_FILENO + 1);
            require(copy == -1 ? errno : 0, "Duplicate process pipe descriptor");
            close();
            value = copy;
        } else {
            require(fcntl(value, F_SETFD, FD_CLOEXEC) == -1 ? errno : 0, "Protect process pipe descriptor");
        }
    }
};

struct SpawnActions {
    posix_spawn_file_actions_t value;
    SpawnActions() { require(posix_spawn_file_actions_init(&value), "Initialize process I/O"); }
    ~SpawnActions() { posix_spawn_file_actions_destroy(&value); }
};

struct SpawnAttributes {
    posix_spawnattr_t value;
    SpawnAttributes() { require(posix_spawnattr_init(&value), "Initialize process attributes"); }
    ~SpawnAttributes() { posix_spawnattr_destroy(&value); }
};

struct ChildProcess {
    pid_t id = -1;
    ~ChildProcess() {
        if (id <= 0) return;
        kill(-id, SIGKILL);
        while (waitpid(id, nullptr, 0) == -1 && errno == EINTR) {}
    }
};

fs::path homeDirectory() {
    if (const auto* home = std::getenv("HOME"); home && fs::path(home).is_absolute()) return home;
    throw std::runtime_error("The home directory is unavailable.");
}

bool executable(const fs::path& path) {
    std::error_code error;
    return fs::is_regular_file(path, error) && access(path.c_str(), X_OK) == 0;
}
}

std::wstring wide(const std::string& text) {
    return std::wstring_convert<std::codecvt_utf8<wchar_t>>{}.from_bytes(text);
}

std::string utf8(const std::wstring& text) {
    return std::wstring_convert<std::codecvt_utf8<wchar_t>>{}.to_bytes(text);
}

std::wstring pathText(const fs::path& path) { return wide(path.native()); }
fs::path textPath(const std::wstring& text) { return fs::path(utf8(text)); }
std::wstring pathKey(const fs::path& path) {
    auto key = pathText(fs::weakly_canonical(path.parent_path()) / path.filename());
#ifdef __APPLE__
    if (pathconf(path.parent_path().c_str(), _PC_CASE_SENSITIVE) == 0)
        for (auto& c : key) c = std::towlower(c);
#endif
    return key;
}

fs::path appDirectory() {
#ifdef __APPLE__
    uint32_t length = 0;
    _NSGetExecutablePath(nullptr, &length);
    std::string path(length, '\0');
    if (_NSGetExecutablePath(path.data(), &length) != 0)
        throw std::runtime_error("Cannot resolve the executable path.");
    return fs::canonical(fs::path(path.c_str())).parent_path();
#else
    return fs::read_symlink("/proc/self/exe").parent_path();
#endif
}

fs::path findExecutable(const char* name) {
#ifdef __APPLE__
    const auto bundled = appDirectory().parent_path() / "Resources" / "tools" / "bin" / name;
    if (executable(bundled)) return bundled;
#if defined(__arm64__) || defined(__aarch64__)
    const fs::path brew = "/opt/homebrew";
#else
    const fs::path brew = "/usr/local";
#endif
    if (std::string_view(name) == "ffmpeg" || std::string_view(name) == "ffprobe") {
        const auto full = brew / "opt/ffmpeg-full/bin" / name;
        if (executable(full)) return full;
    }
#endif
    if (const auto* value = std::getenv("PATH")) {
        std::istringstream directories(value);
        std::string directory;
        while (std::getline(directories, directory, ':')) {
            const auto candidate = fs::path(directory) / name;
            if (candidate.is_absolute() && executable(candidate)) return candidate;
        }
    }
#ifdef __APPLE__
    const auto installed = brew / "bin" / name;
    if (executable(installed)) return installed;
    const auto local = fs::path("/usr/local/bin") / name;
    if (executable(local)) return local;
    const fs::path libreOffice = "/Applications/LibreOffice.app/Contents/MacOS/soffice";
    if (std::string_view(name) == "soffice" && executable(libreOffice)) return libreOffice;
#endif
    return {};
}

fs::path engine(const char* name) {
    auto path = findExecutable(name);
    if (path.empty()) throw std::runtime_error(utf8(tr("systemEngine") + wide(name)));
    return path;
}

fs::path dataDirectory() {
    if (const auto* value = std::getenv("CONTEXT_ACTIONS_DATA_DIR"); value && *value) return value;
#ifdef __APPLE__
    return homeDirectory() / "Library/Application Support/Context Actions";
#else
    if (const auto* value = std::getenv("XDG_DATA_HOME"); value && fs::path(value).is_absolute())
        return fs::path(value) / "context-actions";
    return homeDirectory() / ".local/share/context-actions";
#endif
}

std::wstring readLanguageSetting() {
    std::string value;
    std::ifstream settings(dataDirectory() / "language");
    if (std::getline(settings, value) && !value.empty()) return wide(value);
#ifdef __APPLE__
    const auto preferred = CFLocaleCopyPreferredLanguages();
    char language[128]{};
    if (preferred) {
        if (CFArrayGetCount(preferred) > 0)
            CFStringGetCString(static_cast<CFStringRef>(CFArrayGetValueAtIndex(preferred, 0)),
                               language, sizeof(language), kCFStringEncodingUTF8);
        CFRelease(preferred);
    }
    if (*language) return wide(language);
#else
    for (const auto* key : {"LC_ALL", "LC_MESSAGES", "LANG"}) {
        if (const auto* language = std::getenv(key); language && *language) return wide(language);
    }
#endif
    return L"en";
}

void writeLanguageSetting(const std::wstring& value) {
    std::ofstream settings(dataDirectory() / "language", std::ios::trunc);
    settings << utf8(value) << '\n';
    settings.close();
    if (!settings) throw std::runtime_error("Cannot save the language preference.");
}

void moveFile(const fs::path& from, const fs::path& to, bool replace) {
    if (replace) {
        fs::rename(from, to);
        return;
    }
#ifdef __APPLE__
    if (renamex_np(from.c_str(), to.c_str(), RENAME_EXCL) == 0) return;
    const int error = errno;
    // On a case-insensitive volume the two spellings can refer to the same directory entry.
    if (error == EEXIST && pathKey(from) == pathKey(to) && fs::equivalent(from, to)) {
        fs::rename(from, to);
        return;
    }
#else
    if (syscall(SYS_renameat2, AT_FDCWD, from.c_str(), AT_FDCWD, to.c_str(), RENAME_NOREPLACE) == 0) return;
    const int error = errno;
#endif
    if (error == ENOSYS || error == EINVAL || error == EOPNOTSUPP) {
        // Some mounted filesystems cannot do an exclusive rename. Link creation still fails
        // atomically if the destination already exists.
        if (linkat(AT_FDCWD, from.c_str(), AT_FDCWD, to.c_str(), 0) == 0) {
            if (unlink(from.c_str()) == 0) return;
            const int unlinkError = errno;
            unlink(to.c_str());
            throw fs::filesystem_error("Remove original after linking", from, to,
                                       std::error_code(unlinkError, std::generic_category()));
        }
        throw fs::filesystem_error("Link file", from, to, std::error_code(errno, std::generic_category()));
    }
    throw fs::filesystem_error("Move file", from, to, std::error_code(error, std::generic_category()));
}

void withRenameLock(const fs::path& file, const std::function<void()>& operation) {
    const Descriptor lock{open(file.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600)};
    require(lock.value == -1 ? errno : 0, "Open rename lock");
    if (flock(lock.value, LOCK_EX | LOCK_NB) == -1) {
        const int error = errno;
        if (error == EWOULDBLOCK) throw std::runtime_error(utf8(tr("renameBusy")));
        require(error, "Lock rename journal");
    }
    operation();
}

ProcessResult runProcess(const fs::path& executable, const std::vector<std::wstring>& arguments,
                         std::atomic_bool& cancel, const std::function<void(const std::string&)>& onOutput,
                         uint32_t timeoutMs) {
    if (cancel) throw Cancelled();
    std::vector<std::string> command{executable.native()};
    for (const auto& argument : arguments) command.push_back(utf8(argument));
    std::vector<char*> argv;
    for (auto& argument : command) argv.push_back(argument.data());
    argv.push_back(nullptr);

    int pipeEnds[2];
    require(pipe(pipeEnds) == -1 ? errno : 0, "Open process pipe");
    Descriptor reader{pipeEnds[0]}, writer{pipeEnds[1]};
    reader.closeOnExec();
    writer.closeOnExec();
    require(fcntl(reader.value, F_SETFL, O_NONBLOCK) == -1 ? errno : 0, "Configure process pipe");

    SpawnActions actions;
    require(posix_spawn_file_actions_addopen(&actions.value, STDIN_FILENO, "/dev/null", O_RDONLY, 0), "Redirect process input");
    require(posix_spawn_file_actions_adddup2(&actions.value, writer.value, STDOUT_FILENO), "Redirect process output");
    require(posix_spawn_file_actions_adddup2(&actions.value, writer.value, STDERR_FILENO), "Redirect process errors");
    SpawnAttributes attributes;
    require(posix_spawnattr_setpgroup(&attributes.value, 0), "Set process group");
    require(posix_spawnattr_setflags(&attributes.value, POSIX_SPAWN_SETPGROUP), "Set process group flags");

    ChildProcess child;
    pid_t pid;
#ifdef __APPLE__
    char** environment = *_NSGetEnviron();
#else
    char** environment = environ;
#endif
    require(posix_spawn(&pid, executable.c_str(), &actions.value, &attributes.value, argv.data(), environment), "Start process");
    child.id = pid;
    writer.close();

    using namespace std::chrono;
    const auto deadline = steady_clock::now() + milliseconds(timeoutMs);
    std::string output;
    auto drain = [&] {
        char buffer[4096];
        // Bound each drain so noisy tools cannot delay cancellation or timeouts.
        for (int i = 0; i < 32 && reader.value != -1; ++i) {
            const auto count = read(reader.value, buffer, sizeof(buffer));
            if (count == 0) reader.close();
            if (count <= 0) {
                if (count < 0 && errno == EINTR) continue;
                if (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK) require(errno, "Read process output");
                break;
            }
            if (onOutput) onOutput(std::string(buffer, count));
            output.append(buffer, count);
            if (output.size() > 65536) output.erase(0, output.size() - 65536);
        }
    };

    int status = 0;
    for (;;) {
        if (cancel) throw Cancelled();
        if (timeoutMs != UINT32_MAX && steady_clock::now() >= deadline)
            throw std::runtime_error("Operation timed out.");
        drain();
        const auto exited = waitpid(child.id, &status, WNOHANG);
        if (exited == child.id) {
            child.id = -1;
            drain();
            break;
        }
        if (exited == -1 && errno != EINTR) {
            const int error = errno;
            if (error == ECHILD) child.id = -1;
            require(error, "Wait for process");
        }
        pollfd event{reader.value, POLLIN, 0};
        if (poll(&event, 1, 20) == -1 && errno != EINTR) require(errno, "Wait for process output");
    }
    if (cancel) throw Cancelled();
    const auto code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
    return {static_cast<uint32_t>(code), std::move(output)};
}
}
