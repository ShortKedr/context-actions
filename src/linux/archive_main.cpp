#include "../posix/archive_main.h"
#include <sys/resource.h>

int main(int argc, char** argv) {
    // The reader may allocate decoder state before returning any entries.
    return ca::archiveTool::posixMain(argc, argv, RLIMIT_AS);
}
