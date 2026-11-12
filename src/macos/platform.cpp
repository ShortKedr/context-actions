#include "posix/posix_platform.h"

#include <cerrno>
#include <cstdio>
#include <string_view>
#include <system_error>
#include <CoreFoundation/CoreFoundation.h>
#include <crt_externs.h>
#include <libproc.h>
#include <mach-o/dyld.h>
#include <sys/resource.h>
#include <unistd.h>

namespace ca {
fs::path appDirectory() {
    uint32_t length = 0;
    _NSGetExecutablePath(nullptr, &length);
    std::string path(length, '\0');
    if (_NSGetExecutablePath(path.data(), &length) != 0)
        throw std::runtime_error("Cannot resolve the executable path.");
    return fs::canonical(fs::path(path.c_str())).parent_path();
}

fs::path findExecutable(const char* name) {
    const auto bundled = appDirectory().parent_path() / "Resources" / "tools" / "bin" / name;
    if (posix::executable(bundled)) return bundled;
#if defined(__arm64__) || defined(__aarch64__)
    const fs::path brew = "/opt/homebrew";
#else
    const fs::path brew = "/usr/local";
#endif
    if (std::string_view(name) == "ffmpeg" || std::string_view(name) == "ffprobe") {
        const auto full = brew / "opt/ffmpeg-full/bin" / name;
        if (posix::executable(full)) return full;
    }
    if (auto path = posix::findOnPath(name); !path.empty()) return path;
    const auto installed = brew / "bin" / name;
    if (posix::executable(installed)) return installed;
    const auto local = fs::path("/usr/local/bin") / name;
    if (posix::executable(local)) return local;
    const fs::path libreOffice = "/Applications/LibreOffice.app/Contents/MacOS/soffice";
    if (std::string_view(name) == "soffice" && posix::executable(libreOffice)) return libreOffice;
    return {};
}

fs::path conversionScratchRoot() {
    // Darwin assigns this directory to the current user outside shared output trees.
    const auto length = confstr(_CS_DARWIN_USER_TEMP_DIR, nullptr, 0);
    if (!length) throw std::runtime_error("The user temporary directory is unavailable.");
    std::string path(length, '\0');
    if (confstr(_CS_DARWIN_USER_TEMP_DIR, path.data(), path.size()) != length)
        throw std::runtime_error("The user temporary directory is unavailable.");
    path.pop_back();
    return fs::canonical(path);
}

namespace posix {
bool caseInsensitive(const fs::path& directory) {
    return pathconf(directory.c_str(), _PC_CASE_SENSITIVE) == 0;
}

fs::path defaultDataDirectory() {
    return homeDirectory() / "Library/Application Support/Context Actions";
}

std::wstring preferredLanguage() {
    const auto preferred = CFLocaleCopyPreferredLanguages();
    char language[128]{};
    if (preferred) {
        if (CFArrayGetCount(preferred) > 0)
            CFStringGetCString(static_cast<CFStringRef>(CFArrayGetValueAtIndex(preferred, 0)),
                               language, sizeof(language), kCFStringEncodingUTF8);
        CFRelease(preferred);
    }
    return *language ? wide(language) : L"en";
}

int moveFileExclusive(const fs::path& from, const fs::path& to) {
    if (renamex_np(from.c_str(), to.c_str(), RENAME_EXCL) == 0) return 0;
    const int error = errno;
    // On a case-insensitive volume the two spellings can refer to the same directory entry.
    if (error == EEXIST && pathKey(from) == pathKey(to) && fs::equivalent(from, to)) {
        fs::rename(from, to);
        return 0;
    }
    return error;
}

char** processEnvironment() { return *_NSGetEnviron(); }

void checkMemoryLimit(pid_t pid, size_t limit) {
    if (!limit) return;
    // Older supported macOS versions do not enforce an address-space rlimit.
    rusage_info_v0 usage{};
    if (proc_pid_rusage(pid, RUSAGE_INFO_V0, reinterpret_cast<rusage_info_t*>(&usage)) == 0) {
        if (usage.ri_phys_footprint > limit) throw std::runtime_error("Operation exceeded its memory budget.");
    } else if (errno != ESRCH && errno != EINTR) {
        throw std::system_error(errno, std::generic_category(), "Measure process memory");
    }
}
}
}
