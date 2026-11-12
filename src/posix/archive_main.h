#pragma once
#include <optional>

namespace ca::archiveTool {
int posixMain(int argc, char** argv, std::optional<int> memoryResource = std::nullopt);
}
