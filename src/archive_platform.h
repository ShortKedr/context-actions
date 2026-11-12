#pragma once
#include <archive.h>
#include <zlib.h>
#include <cstdio>
#include <cstdint>
#include <filesystem>

namespace ca::archiveTool {
namespace fs = std::filesystem;
inline constexpr std::uint64_t gibibyte = 1024ULL * 1024 * 1024;
inline constexpr std::uint64_t maxContainerBytes = 9 * gibibyte;

std::FILE* openFile(const fs::path& path, bool write);
gzFile openGzip(const fs::path& path);
int openReader(archive* reader, const fs::path& path);
void repack(const fs::path& from, const fs::path& to, const fs::path& source, const fs::path& destination);
}
