#include "posix_platform.h"

#include <array>
#include <cerrno>
#include <chrono>
#include <codecvt>
#include <cstdio>
#include <cstdlib>
#include <cwctype>
#include <fstream>
#include <locale>
#include <sstream>
#include <system_error>

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

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

void protectPrivateMode(int descriptor, mode_t allowed) {
    struct stat info{};
    require(fstat(descriptor, &info) == -1 ? errno : 0, "Inspect private path");
    if (info.st_uid != geteuid()) throw std::runtime_error("Private path belongs to another user.");
    if (info.st_mode & 0777 & ~allowed) {
        require(fchmod(descriptor, info.st_mode & allowed) == -1 ? errno : 0, "Set private permissions");
        require(fstat(descriptor, &info) == -1 ? errno : 0, "Check private permissions");
        if (info.st_mode & 0777 & ~allowed) throw std::runtime_error("The filesystem does not support private permissions.");
    }
}

void protectPrivateDirectory(const fs::path& path) {
    const Descriptor directory{open(path.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)};
    require(directory.value == -1 ? errno : 0, "Open private directory");
    protectPrivateMode(directory.value, 0700);
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
    if (posix::caseInsensitive(path.parent_path()))
        for (auto& c : key) c = std::towlower(c);
    return key;
}

namespace posix {
fs::path homeDirectory() {
    if (const auto* home = std::getenv("HOME"); home && fs::path(home).is_absolute()) return home;
    throw std::runtime_error("The home directory is unavailable.");
}

bool executable(const fs::path& path) {
    std::error_code error;
    return fs::is_regular_file(path, error) && access(path.c_str(), X_OK) == 0;
}

fs::path findOnPath(const char* name) {
    if (const auto* value = std::getenv("PATH")) {
        std::istringstream directories(value);
        std::string directory;
        while (std::getline(directories, directory, ':')) {
            const auto candidate = fs::path(directory) / name;
            if (candidate.is_absolute() && executable(candidate)) return candidate;
        }
    }
    return {};
}
}

fs::path engine(const char* name) {
    auto path = findExecutable(name);
    if (path.empty()) throw std::runtime_error(utf8(tr("systemEngine") + wide(name)));
    return path;
}

fs::path archiveExecutable() { return appDirectory() / "ContextActionsArchive"; }

fs::path dataDirectory() {
    if (const auto* value = std::getenv("CONTEXT_ACTIONS_DATA_DIR"); value && *value) return value;
    return posix::defaultDataDirectory();
}

void createPrivateDirectory(const fs::path& path) {
    if (mkdir(path.c_str(), 0700) == -1)
        throw fs::filesystem_error("Create private directory", path, std::error_code(errno, std::generic_category()));
    try {
        protectPrivateDirectory(path);
    } catch (...) {
        rmdir(path.c_str());
        throw;
    }
}

void ensurePrivateDirectory(const fs::path& requested) {
    const auto path = requested.filename().empty() && requested.has_relative_path() ? requested.parent_path() : requested;
    if (path.filename().empty() || path.filename() == "." || path.filename() == "..")
        throw std::runtime_error("State requires a dedicated directory.");
    if (!path.parent_path().empty()) fs::create_directories(path.parent_path());
    const bool created = mkdir(path.c_str(), 0700) == 0;
    if (!created && errno != EEXIST)
        throw fs::filesystem_error("Create private directory", path, std::error_code(errno, std::generic_category()));
    try {
        protectPrivateDirectory(path);
    } catch (...) {
        if (created) rmdir(path.c_str());
        throw;
    }
}

File openNewPrivateFile(const fs::path& path) {
    Descriptor descriptor{open(path.c_str(), O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0600)};
    if (descriptor.value == -1)
        throw fs::filesystem_error("Create new file", path, std::error_code(errno, std::generic_category()));
    protectPrivateMode(descriptor.value, 0600);
    auto* file = fdopen(descriptor.value, "wb");
    if (!file) throw fs::filesystem_error("Open new file stream", path, std::error_code(errno, std::generic_category()));
    descriptor.value = -1; // The stream owns the descriptor now.
    return File(file);
}

std::wstring readLanguageSetting() {
    std::string value;
    const auto path = dataDirectory() / "language";
    std::error_code error;
    if (fs::is_regular_file(fs::symlink_status(path, error))) {
        std::ifstream settings(path);
        if (std::getline(settings, value) && !value.empty()) return wide(value);
    }
    return posix::preferredLanguage();
}

void writeLanguageSetting(const std::wstring& value) {
    const auto path = dataDirectory() / "language";
    const auto status = fs::symlink_status(path);
    if (fs::exists(status) && (!fs::is_regular_file(status) || fs::hard_link_count(path) != 1))
        throw std::runtime_error("Invalid language settings file.");
    std::ofstream settings(path, std::ios::trunc);
    settings << utf8(value) << '\n';
    settings.close();
    if (!settings) throw std::runtime_error("Cannot save the language preference.");
}

void moveFile(const fs::path& from, const fs::path& to, bool replace) {
    if (replace) {
        fs::rename(from, to);
        return;
    }
    const int error = posix::moveFileExclusive(from, to);
    if (!error) return;
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

void publishFile(const fs::path& from, const fs::path& to) {
    try {
        moveFile(from, to);
        return;
    } catch (const fs::filesystem_error& error) {
        if (error.code().value() != EXDEV) throw;
    }

    // Across filesystems, exclusive creation pins the destination even in a shared directory.
    std::ifstream input(from, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot open the completed output.");
    auto output = openNewPrivateFile(to);
    std::array<char, 64 * 1024> buffer;
    while (input.read(buffer.data(), buffer.size()) || input.gcount()) {
        const auto count = static_cast<size_t>(input.gcount());
        if (std::fwrite(buffer.data(), 1, count, output.get()) != count)
            throw std::runtime_error("Cannot copy the completed output.");
    }
    if (input.bad() || !input.eof() || std::fclose(output.release()) != 0)
        throw std::runtime_error("Cannot finish copying the completed output.");
}

void withRenameLock(const fs::path& file, const std::function<void()>& operation) {
    const Descriptor lock{open(file.c_str(), O_CREAT | O_RDWR | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK, 0600)};
    require(lock.value == -1 ? errno : 0, "Open rename lock");
    struct stat info{};
    require(fstat(lock.value, &info) == -1 ? errno : 0, "Inspect rename lock");
    if (!S_ISREG(info.st_mode) || info.st_uid != geteuid() || info.st_nlink != 1)
        throw std::runtime_error("Invalid rename lock file.");
    if (flock(lock.value, LOCK_EX | LOCK_NB) == -1) {
        const int error = errno;
        if (error == EWOULDBLOCK) throw std::runtime_error(utf8(tr("renameBusy")));
        require(error, "Lock rename journal");
    }
    operation();
}

ProcessResult runProcess(const fs::path& executable, const std::vector<std::wstring>& arguments,
                         std::atomic_bool& cancel, const std::function<void(const std::string&)>& onOutput,
                         uint32_t timeoutMs, size_t processMemoryLimit) {
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
    require(posix_spawn(&pid, executable.c_str(), &actions.value, &attributes.value, argv.data(),
                        posix::processEnvironment()), "Start process");
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
        posix::checkMemoryLimit(child.id, processMemoryLimit);
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
