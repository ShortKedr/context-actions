#pragma once
#include "core.h"

namespace ca {
fs::path engine(const char* name);
void moveFile(const fs::path& from, const fs::path& to, bool replace = false);
void withRenameLock(const fs::path& file, const std::function<void()>& operation);
std::wstring pathKey(const fs::path& path);
std::wstring readLanguageSetting();
void writeLanguageSetting(const std::wstring& value);
std::wstring officeUnavailableReason(bool excel);
const char* officeActionKey(bool excel);
void createImagePdf(const fs::path& image, const fs::path& output, int width, int height);
}
