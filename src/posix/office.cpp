#include "posix_platform.h"

#include <fstream>

namespace ca {
namespace {
std::atomic<Capability> capabilities[2]{Capability::unknown, Capability::unknown};

std::wstring fileUrl(const fs::path& path) {
    constexpr wchar_t hex[] = L"0123456789ABCDEF";
    std::wstring url = L"file://";
    for (unsigned char c : path.native()) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~' || c == '/') {
            url += static_cast<wchar_t>(c);
        } else {
            url += L'%';
            url += hex[c >> 4];
            url += hex[c & 15];
        }
    }
    return url;
}
}

Capability officeCapability(bool excel) {
    const auto state = capabilities[excel].load();
    if (state != Capability::unknown) return state;
    return findExecutable("soffice").empty() ? Capability::unavailable : Capability::available;
}

void setOfficeCapability(bool excel, Capability state) { capabilities[excel] = state; }
void resetOfficeCapabilities() {
    setOfficeCapability(false, Capability::unknown);
    setOfficeCapability(true, Capability::unknown);
}

std::wstring officeUnavailableReason(bool) { return tr("needLibreOffice"); }
const char* officeActionKey(bool excel) { return excel ? "calcPdf" : "writerPdf"; }

void exportOfficePdf(const fs::path& input, const fs::path& output, bool excel, std::atomic_bool& cancel) {
    if (cancel) throw Cancelled();
    const auto soffice = findExecutable("soffice");
    if (soffice.empty()) throw std::runtime_error(utf8(officeUnavailableReason(excel)));

    const auto profile = output.parent_path() / "office-profile";
    const auto generated = output.parent_path() / "office-export";
    fs::create_directories(profile / "user");
    fs::create_directory(generated);
    std::ofstream settings(profile / "user/registrymodifications.xcu");
    // Use a fresh profile with the highest macro-security setting and no trusted document directories.
    settings << R"(<?xml version="1.0" encoding="UTF-8"?>
<oor:items xmlns:oor="http://openoffice.org/2001/registry">
  <item oor:path="/org.openoffice.Office.Common/Security/Scripting">
    <prop oor:name="MacroSecurityLevel" oor:op="fuse"><value>3</value></prop>
  </item>
  <item oor:path="/org.openoffice.Office.Writer/Content/Update">
    <prop oor:name="Link" oor:op="fuse"><value>1</value></prop>
  </item>
  <item oor:path="/org.openoffice.Office.Calc/Content/Update">
    <prop oor:name="Link" oor:op="fuse"><value>1</value></prop>
  </item>
</oor:items>
)";
    settings.close();
    if (!settings) throw std::runtime_error("Cannot write the LibreOffice conversion profile.");

    const auto process = runProcess(soffice, {
        L"-env:UserInstallation=" + fileUrl(profile), L"--headless", L"--norestore", L"--convert-to",
        excel ? L"pdf:calc_pdf_Export" : L"pdf:writer_pdf_Export", L"--outdir", pathText(generated), pathText(input)
    }, cancel, {}, 120000);
    const auto pdf = generated / input.filename().replace_extension(".pdf");
    if (process.code != 0 || !fs::is_regular_file(pdf) || fs::file_size(pdf) == 0)
        throw std::runtime_error(utf8(tr("failed")) + ": " + process.output);
    moveFile(pdf, output);
}
}
