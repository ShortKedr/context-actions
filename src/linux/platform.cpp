#include "posix/posix_platform.h"

#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <system_error>

extern char** environ;

namespace ca {
fs::path appDirectory() { return fs::read_symlink("/proc/self/exe").parent_path(); }
fs::path findExecutable(const char* name) { return posix::findOnPath(name); }

fs::path conversionScratchRoot() {
    const auto home = fs::canonical(posix::homeDirectory());
    for (auto path = home; ; path = path.parent_path()) {
        struct stat info{};
        if (stat(path.c_str(), &info) == -1)
            throw std::system_error(errno, std::generic_category(), "Inspect scratch directory ancestry");
        if (!S_ISDIR(info.st_mode) || (info.st_uid != 0 && info.st_uid != geteuid()) ||
            ((info.st_mode & 0022) && !(info.st_mode & S_ISVTX)))
            throw std::runtime_error("The user scratch directory is in an untrusted location.");
        if (path == path.root_path()) break;
    }
    return home;
}

namespace posix {
bool caseInsensitive(const fs::path&) { return false; }

fs::path defaultDataDirectory() {
    if (const auto* value = std::getenv("XDG_DATA_HOME"); value && fs::path(value).is_absolute())
        return fs::path(value) / "context-actions";
    return homeDirectory() / ".local/share/context-actions";
}

std::wstring preferredLanguage() {
    for (const auto* key : {"LC_ALL", "LC_MESSAGES", "LANG"}) {
        if (const auto* language = std::getenv(key); language && *language) return wide(language);
    }
    return L"en";
}

int moveFileExclusive(const fs::path& from, const fs::path& to) {
    return syscall(SYS_renameat2, AT_FDCWD, from.c_str(), AT_FDCWD, to.c_str(), RENAME_NOREPLACE) == 0 ? 0 : errno;
}

char** processEnvironment() { return environ; }
// The archive helper enforces its own address-space limit on Linux.
void checkMemoryLimit(pid_t, size_t) {}
}
}
