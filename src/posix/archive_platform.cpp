#include "../archive_platform.h"
#include "archive_main.h"

#include <clocale>
#include <cstdlib>
#include <stdexcept>
#include <sys/resource.h>

namespace ca::archiveTool {
namespace {
void lowerResourceLimit(int resource, rlim_t limit) {
    rlimit limits{};
    if (getrlimit(resource, &limits) != 0) throw std::runtime_error("Cannot read archive process resource limit");
    if (limits.rlim_cur != RLIM_INFINITY && limits.rlim_cur <= limit) return;
    limits.rlim_cur = limit;
    if (setrlimit(resource, &limits) != 0) throw std::runtime_error("Cannot set archive process resource limit");
}
}

std::FILE* openFile(const fs::path& path, bool write) {
    return std::fopen(path.c_str(), write ? "wb" : "rb");
}

gzFile openGzip(const fs::path& path) { return gzopen(path.c_str(), "rb"); }

int openReader(archive* reader, const fs::path& path) {
    return archive_read_open_filename(reader, path.c_str(), 64 * 1024);
}

int posixMain(int argc, char** argv, std::optional<int> memoryResource) {
    std::setlocale(LC_CTYPE, "");
    if (MB_CUR_MAX == 1 && !std::setlocale(LC_CTYPE, "C.UTF-8")) std::setlocale(LC_CTYPE, "UTF-8");
    if (argc != 5) return 2;
    try {
        // Also bound temporary files written directly by the 7z encoder.
        lowerResourceLimit(RLIMIT_FSIZE, maxContainerBytes);
        if (memoryResource) lowerResourceLimit(*memoryResource, gibibyte);
        repack(argv[1], argv[2], argv[3], argv[4]);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
}
