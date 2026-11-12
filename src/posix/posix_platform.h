#pragma once
#include "platform.h"
#include <sys/types.h>

namespace ca {
fs::path findExecutable(const char* name);

namespace posix {
fs::path homeDirectory();
bool executable(const fs::path& path);
fs::path findOnPath(const char* name);
bool caseInsensitive(const fs::path& directory);
fs::path defaultDataDirectory();
std::wstring preferredLanguage();
int moveFileExclusive(const fs::path& from, const fs::path& to);
char** processEnvironment();
void checkMemoryLimit(pid_t pid, size_t limit);
}
}
