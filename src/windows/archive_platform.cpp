#include "../archive_platform.h"

#include <clocale>
#include <exception>

namespace ca::archiveTool {
std::FILE* openFile(const fs::path& path, bool write) {
    std::FILE* file = nullptr;
    return _wfopen_s(&file, path.c_str(), write ? L"wb" : L"rb") == 0 ? file : nullptr;
}

gzFile openGzip(const fs::path& path) { return gzopen_w(path.c_str(), "rb"); }

int openReader(archive* reader, const fs::path& path) {
    return archive_read_open_filename_w(reader, path.c_str(), 64 * 1024);
}
}

int wmain(int argc, wchar_t** argv) {
    std::setlocale(LC_CTYPE, ".UTF-8");
    if (argc != 5) return 2;
    try {
        ca::archiveTool::repack(argv[1], argv[2], argv[3], argv[4]);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
