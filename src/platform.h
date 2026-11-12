#pragma once
#include "core.h"
#include <cstdio>
#include <memory>

namespace ca {
struct CloseFile {
    void operator()(std::FILE* file) const { std::fclose(file); }
};
using File = std::unique_ptr<std::FILE, CloseFile>;

fs::path engine(const char* name);
fs::path conversionScratchRoot();
void createPrivateDirectory(const fs::path& path);
void ensurePrivateDirectory(const fs::path& path);
File openNewPrivateFile(const fs::path& path);
void moveFile(const fs::path& from, const fs::path& to, bool replace = false);
void publishFile(const fs::path& from, const fs::path& to);
void withRenameLock(const fs::path& file, const std::function<void()>& operation);
std::wstring pathKey(const fs::path& path);
std::wstring readLanguageSetting();
void writeLanguageSetting(const std::wstring& value);
std::wstring officeUnavailableReason(bool excel);
const char* officeActionKey(bool excel);
void createImagePdf(const fs::path& image, const fs::path& output, int width, int height);
}
