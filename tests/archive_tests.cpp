#include "core.h"
#include <archive.h>
#include <archive_entry.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <clocale>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <string_view>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#endif

namespace {
namespace fs = ca::fs;

void expect(bool condition, std::string message) {
    if (!condition) throw std::runtime_error(std::move(message));
}

void checked(archive* handle, int status, const char* operation) {
    if (status == ARCHIVE_OK) return;
    const auto* detail = archive_error_string(handle);
    throw std::runtime_error(std::string(operation) + ": " + (detail ? detail : "unexpected archive status"));
}

std::string read(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    expect(bool(stream), "Cannot read " + ca::utf8(ca::pathText(path)));
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

void write(const fs::path& path, std::string_view bytes) {
    std::ofstream stream(path, std::ios::binary);
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    stream.close();
    expect(bool(stream), "Cannot write archive fixture");
}

struct TestDirectory {
    fs::path path;
    explicit TestDirectory(const fs::path& parent) {
        fs::create_directories(parent);
        path = parent / ("run-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        expect(fs::create_directory(path), "Cannot create isolated test directory");
    }
    ~TestDirectory() {
        std::error_code error;
        fs::remove_all(path, error);
    }
};

enum class EntryType { file, directory, symlink };
struct Entry {
    EntryType type;
    std::string data;
    bool operator==(const Entry&) const = default;
};
using Entries = std::map<std::string, Entry>;

void makeArchive(const fs::path& path, std::wstring_view format, const Entries& entries) {
    std::unique_ptr<archive, decltype(&archive_write_free)> writer(archive_write_new(), archive_write_free);
    auto* handle = writer.get();
    expect(handle != nullptr, "Cannot allocate fixture writer");
    if (format == L"zip") {
        checked(handle, archive_write_set_format_zip(handle), "Set ZIP fixture format");
        checked(handle, archive_write_set_format_option(handle, "zip", "hdrcharset", "UTF-8"),
                "Set UTF-8 ZIP fixture headers");
        checked(handle, archive_write_zip_set_compression_store(handle), "Set stored ZIP fixture compression");
    } else if (format == L"7z") {
        checked(handle, archive_write_set_format_7zip(handle), "Set 7z fixture format");
    } else {
        checked(handle, archive_write_set_format_pax_restricted(handle), "Set TAR fixture format");
        if (format == L"tar.gz")
            checked(handle, archive_write_add_filter_gzip(handle), "Set gzip fixture filter");
    }
#ifdef _WIN32
    checked(handle, archive_write_open_filename_w(handle, path.c_str()), "Open fixture file");
#else
    checked(handle, archive_write_open_filename(handle, path.c_str()), "Open fixture file");
#endif
    for (const auto& [name, contents] : entries) {
        std::unique_ptr<archive_entry, decltype(&archive_entry_free)> entry(archive_entry_new(), archive_entry_free);
        expect(entry != nullptr, "Cannot allocate fixture entry");
        archive_entry_set_pathname_utf8(entry.get(), name.c_str());
        archive_entry_set_mtime(entry.get(), 1704067200, 0);
        archive_entry_set_perm(entry.get(), contents.type == EntryType::directory ? 0755 : 0644);
        if (contents.type == EntryType::symlink) {
            archive_entry_set_filetype(entry.get(), AE_IFLNK);
            archive_entry_set_symlink(entry.get(), contents.data.c_str());
            archive_entry_set_size(entry.get(), 0);
        } else {
            archive_entry_set_filetype(entry.get(), contents.type == EntryType::directory ? AE_IFDIR : AE_IFREG);
            archive_entry_set_size(entry.get(), static_cast<la_int64_t>(contents.data.size()));
        }
        checked(handle, archive_write_header(handle, entry.get()), "Write fixture header");
        if (contents.type == EntryType::file && !contents.data.empty())
            expect(archive_write_data(handle, contents.data.data(), contents.data.size()) ==
                   static_cast<la_ssize_t>(contents.data.size()), "Write fixture payload");
        checked(handle, archive_write_finish_entry(handle), "Finish fixture entry");
    }
    checked(handle, archive_write_close(handle), "Close fixture file");
}

void verifyArchive(const fs::path& path, std::wstring_view format, const Entries& expected) {
    const auto bytes = read(path);
    std::unique_ptr<archive, decltype(&archive_read_free)> reader(archive_read_new(), archive_read_free);
    auto* handle = reader.get();
    expect(handle != nullptr, "Cannot allocate verification reader");
    checked(handle, archive_read_support_format_tar(handle), "Enable TAR verification");
    checked(handle, archive_read_support_format_zip(handle), "Enable ZIP verification");
    checked(handle, archive_read_support_format_7zip(handle), "Enable 7z verification");
    checked(handle, archive_read_support_filter_gzip(handle), "Enable gzip verification");
    checked(handle, archive_read_open_memory(handle, bytes.data(), bytes.size()), "Read resulting archive");
    Entries actual;
    for (;;) {
        archive_entry* entry = nullptr;
        const int status = archive_read_next_header(handle, &entry);
        if (status == ARCHIVE_EOF) break;
        checked(handle, status, "Read output header");
        const auto* pathname = archive_entry_pathname_utf8(entry);
        expect(pathname != nullptr, "Result archive name must be UTF-8");
        std::string name(pathname);
        const auto filetype = archive_entry_filetype(entry);
        expect(filetype == AE_IFREG || filetype == AE_IFDIR, "Unexpected output entry type");
        if (filetype == AE_IFDIR && name.ends_with('/')) name.pop_back();
        std::string data;
        std::array<char, 32768> buffer{};
        for (;;) {
            const auto count = archive_read_data(handle, buffer.data(), buffer.size());
            if (count == 0) break;
            if (count < 0) checked(handle, static_cast<int>(count), "Read output data");
            data.append(buffer.data(), static_cast<size_t>(count));
        }
        expect(actual.emplace(name, Entry{filetype == AE_IFDIR ? EntryType::directory : EntryType::file,
                                           std::move(data)}).second, "Duplicate entry in output: " + name);
    }
    const int expectedFormat = format == L"zip" ? ARCHIVE_FORMAT_ZIP :
                               format == L"7z" ? ARCHIVE_FORMAT_7ZIP : ARCHIVE_FORMAT_TAR;
    expect((archive_format(handle) & ARCHIVE_FORMAT_BASE_MASK) == expectedFormat,
           "Output bytes must match the selected archive format");
    expect(archive_filter_code(handle, 0) == (format == L"tar.gz" ? ARCHIVE_FILTER_GZIP : ARCHIVE_FILTER_NONE),
           "Output must use the selected compression filter");
    checked(handle, archive_read_close(handle), "Close output reader");
    expect(actual == expected, "Output entries, paths, and bytes must match the source fixture");
}

ca::Options job(std::vector<fs::path> inputs, const fs::path& output, std::wstring format) {
    ca::Options options;
    options.action = ca::Action::repack;
    options.files = std::move(inputs);
    options.output = output;
    options.format = std::move(format);
    return options;
}

void rejects(const ca::Options& options, const char* condition) {
    try {
        ca::validate(options);
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(std::string(condition) + " should be rejected by validation");
}

void failedRepack(const fs::path& input, const fs::path& output, std::wstring format, const std::string& condition) {
    const auto original = read(input);
    std::atomic_bool cancel = false;
    bool failureReported = false;
    const auto result = ca::execute(job({input}, output, std::move(format)), cancel, [&](const ca::Update& update) {
        failureReported |= update.failed && !update.status.empty();
    });
    expect(result.failed == 1 && result.succeeded == 0 && !result.cancelled && result.outputs.empty() &&
           failureReported, condition + " must produce a reported failure");
    expect(fs::is_empty(output), condition + " left a partial result");
    expect(read(input) == original, condition + " modified the source");
}

void runTests(const fs::path& root, const fs::path& fixtures) {
    const auto inputDirectory = root / ca::textPath(L"Вход & 資料");
    const auto outputDirectory = root / ca::textPath(L"Результат & 輸出");
    fs::create_directory(inputDirectory);
    fs::create_directory(outputDirectory);

    std::string payload(192000, '\0');
    for (size_t i = 0; i < payload.size(); ++i) payload[i] = static_cast<char>(i % 251);
    const auto name = ca::utf8(L"nested/данные-資料.bin");
    const Entries standard = {
        {"empty-dir", {EntryType::directory, {}}},
        {"empty.txt", {EntryType::file, {}}},
        {"nested", {EntryType::directory, {}}},
        {name, {EntryType::file, payload}}
    };
    const Entries rarEntries = {
        {"empty-dir", {EntryType::directory, {}}},
        {"empty.txt", {EntryType::file, {}}},
        {"folder", {EntryType::directory, {}}},
        {"folder/data.bin", {EntryType::file, std::string("RAR fixture\x00\xff\r\n", 15)}}
    };
    Entries solidRarEntries;
    for (int file = 1; file <= 4; ++file) {
        std::string contents;
        for (int index = 1; index <= 1024; ++index) {
            const auto value = static_cast<uint32_t>(std::max(0, index * index - 3 * index + 1 + file));
            for (unsigned shift = 0; shift < 32; shift += 8)
                contents.push_back(static_cast<char>((value >> shift) & 0xff));
        }
        solidRarEntries.emplace("test" + std::to_string(file) + ".bin", Entry{EntryType::file, std::move(contents)});
    }
    std::string compressedPayload;
    for (int line = 0; line < 64; ++line) compressedPayload += "Context Actions archive fixture.\n";
    const Entries compressedEntries = {{"fixture.txt", {EntryType::file, compressedPayload}}};
    const std::array<std::wstring, 3> targets = {L"zip", L"tar.gz", L"7z"};
    const auto matrix = [&](const std::wstring& stem, const std::wstring& suffix,
                            const std::wstring& inputFormat, const Entries& expected,
                            const fs::path& fixture = fs::path()) {
        const auto input = inputDirectory / ca::textPath(stem + L"." + suffix);
        if (fixture.empty()) makeArchive(input, inputFormat, expected);
        else fs::copy_file(fixture, input);
        const auto original = read(input);
        std::atomic_bool cancel = false;
        for (const auto& target : targets) {
            if (inputFormat == target) continue;
            const auto output = outputDirectory / ca::textPath(stem + L"." + target);
            const auto label = ca::utf8(suffix + L" → " + target);
            std::wstring diagnostic;
            const auto result = ca::execute(job({input}, outputDirectory, target), cancel, [&](const ca::Update& update) {
                if (update.failed) diagnostic = update.status;
            });
            expect(result.succeeded == 1 && result.failed == 0 && !result.cancelled &&
                   result.outputs == std::vector<fs::path>{output}, label + " failed: " + ca::utf8(diagnostic));
            verifyArchive(output, target, expected);
            expect(read(input) == original, label + " modified the source");
        }
    };
    matrix(L"данные-zip", L"ZIP", L"zip", standard);
    matrix(L"данные-targz", L"tar.gz", L"tar.gz", standard);
    matrix(L"данные-tgz", L"TGZ", L"tar.gz", standard);
    matrix(L"данные-tar", L"tar", L"tar", standard);
    matrix(L"данные-7z", L"7z", L"7z", standard);
    matrix(L"данные-rar4", L"rar", L"rar", rarEntries, fixtures / "stored-rar4.rar");
    matrix(L"данные-rar5", L"RAR", L"rar", rarEntries, fixtures / "stored-rar5.rar");
    matrix(L"compressed-rar4", L"rar", L"rar", {{"testdir/test.txt", {EntryType::file, "test text document\r\n"}}},
           fixtures / "compressed-rar4.rar");
    matrix(L"solid-rar5", L"rar", L"rar", solidRarEntries, fixtures / "solid-rar5.rar");
    matrix(L"deflate", L"zip", L"zip", compressedEntries, fixtures / "deflate.zip");
    matrix(L"lzma2", L"7z", L"7z", compressedEntries, fixtures / "lzma2.7z");
    matrix(L"empty-zip", L"zip", L"zip", {});
    matrix(L"empty-targz", L"tar.gz", L"tar.gz", {});
    matrix(L"empty-tar", L"tar", L"tar", {});
    matrix(L"empty-7z", L"7z", L"7z", {});
    matrix(L"only-directory", L"tar", L"tar", {{"empty", {EntryType::directory, {}}}});

    const Entries firstArchive = {{"first.txt", {EntryType::file, "first archive"}}};
    const Entries secondArchive = {{"second.txt", {EntryType::file, "second archive"}}};
    Entries combined = firstArchive;
    combined.insert(secondArchive.begin(), secondArchive.end());
    for (const auto* format : {L"tar", L"tar.gz"}) {
        const auto first = root / "first-archive";
        const auto second = root / "second-archive";
        const auto concatenated = root / "concatenated";
        makeArchive(first, format, firstArchive);
        makeArchive(second, format, secondArchive);
        write(concatenated, read(first) + read(second));
        matrix(L"concatenated-" + std::wstring(format), format, format, combined, concatenated);
    }

    const auto dotRoot = inputDirectory / "dot-root.tar";
    makeArchive(dotRoot, L"tar", {{".", {EntryType::directory, {}}},
                                 {"./item.txt", {EntryType::file, "content"}}});
    std::atomic_bool cancel = false;
    const auto dotResult = ca::execute(job({dotRoot}, outputDirectory, L"zip"), cancel, {});
    expect(dotResult.succeeded == 1, "Common TAR root marker and leading ./ path are accepted");
    verifyArchive(dotResult.outputs.at(0), L"zip", {{"item.txt", {EntryType::file, "content"}}});

    const auto zip = inputDirectory / ca::textPath(L"данные-zip.ZIP");
    const auto gzip = inputDirectory / ca::textPath(L"данные-targz.tar.gz");
    const auto seven = inputDirectory / ca::textPath(L"данные-7z.7z");
    expect(ca::outputFormats(ca::Action::repack, {zip, gzip}) == std::vector<std::wstring>{L"7z"},
           "Batch uses one target different from each input format");
    expect(ca::outputFormats(ca::Action::repack, {zip, gzip, seven}).empty(),
           "No different common target across all target formats");
    rejects(job({zip}, outputDirectory, L"zip"), "Unchanged archive format");
    rejects(job({gzip}, outputDirectory, L"tar.gz"), "Unchanged compound archive format");
    rejects(job({seven}, outputDirectory, L"7z"), "Unchanged 7z format");
    rejects(job({inputDirectory / ca::textPath(L"данные-tgz.TGZ")}, outputDirectory, L"tar.gz"), "Same format alias");
    rejects(job({zip}, outputDirectory, L"rar"), "Unsupported RAR output");
    rejects(job({zip, gzip}, outputDirectory, L"tar.gz"), "Target matches a file in the batch");
    rejects(job({zip, inputDirectory / "ordinary.txt"}, outputDirectory, L"7z"), "Mixed file selection");

    const auto batchOutput = root / "batch";
    fs::create_directory(batchOutput);
    const auto collision = batchOutput / ca::textPath(L"данные-zip.7z");
    write(collision, "keep existing output");
    const auto report = root / "batch-report.txt";
    ca::CommandLine command;
    ca::parseArguments({L"--action", L"repack", L"--headless", L"--report", ca::pathText(report),
                        L"--output", ca::pathText(batchOutput), L"--", ca::pathText(zip), ca::pathText(gzip)}, command);
    expect(ca::runCommandLine(command) == 0 && read(report).find("success=2 failed=0 cancelled=0") != std::string::npos,
           "Headless batch selects its only common target and reports both successes");
    expect(read(collision) == "keep existing output", "Existing output must be preserved");
    verifyArchive(batchOutput / ca::textPath(L"данные-zip (2).7z"), L"7z", standard);
    verifyArchive(batchOutput / ca::textPath(L"данные-targz.7z"), L"7z", standard);

    ca::CommandLine direct;
    const auto explicitReport = root / "explicit-report.txt";
    const auto rar = inputDirectory / ca::textPath(L"данные-rar4.rar");
    ca::parseArguments({L"--action", L"repack", L"--headless", L"--format", L"ZIP", L"--report",
                        ca::pathText(explicitReport), ca::pathText(rar)}, direct);
    expect(ca::runCommandLine(direct) == 0, "Headless repack supports an explicit case-insensitive target");
    verifyArchive(inputDirectory / ca::textPath(L"данные-rar4.zip"), L"zip", rarEntries);
    const auto savedJob = root / "repack.caj";
    ca::saveOptions(direct.options, savedJob);
    const auto reloaded = ca::readJob(savedJob);
    expect(reloaded.action == ca::Action::repack && reloaded.files == direct.options.files &&
           reloaded.format == direct.options.format, "Repack job preserves inputs and target");
    bool invalidImmediate = false;
    try {
        ca::CommandLine immediate;
        ca::parseArguments({L"--action", L"repack", L"--run", L"--format", L"zip", ca::pathText(zip)}, immediate);
    } catch (const std::exception&) {
        invalidImmediate = true;
    }
    expect(invalidImmediate, "Immediate repack rejects a target identical to the source format");

    const auto failureDirectory = root / "failure";
    fs::create_directory(failureDirectory);
    const auto rejectArchive = [&](const fs::path& input, const std::string& condition) {
        for (const auto& target : ca::outputFormats(ca::Action::repack, {input}))
            failedRepack(input, failureDirectory, target, condition + " to " + ca::utf8(target));
    };
    const auto disguised = inputDirectory / "disguised.tar";
    fs::copy_file(zip, disguised);
    rejectArchive(disguised, "Signature and extension mismatch");
    const auto broken = inputDirectory / "broken.zip";
    auto brokenBytes = read(zip);
    const auto payloadStart = brokenBytes.find(payload);
    expect(payloadStart != std::string::npos, "Stored ZIP fixture contains the original payload");
    brokenBytes[payloadStart + payload.size() - 1] ^= 1;
    write(broken, brokenBytes);
    rejectArchive(broken, "Bad CRC after valid entries");

    const auto truncatedZip = inputDirectory / "truncated.zip";
    write(truncatedZip, std::string_view(read(zip)).substr(0, 30));
    rejectArchive(truncatedZip, "Truncated ZIP file");
    const auto truncatedSeven = inputDirectory / "truncated.7z";
    write(truncatedSeven, std::string_view(read(seven)).substr(0, 20));
    rejectArchive(truncatedSeven, "Truncated 7z start header");
    const auto truncatedRar = inputDirectory / "truncated.rar";
    write(truncatedRar, std::string_view(read(fixtures / "stored-rar5.rar")).substr(0, 12));
    rejectArchive(truncatedRar, "Truncated RAR main header");

    const auto badGzip = inputDirectory / "bad-trailer.tar.gz";
    auto badTrailer = read(gzip);
    expect(badTrailer.size() > 8, "Fixture contains a gzip trailer");
    badTrailer[badTrailer.size() - 8] ^= 1;
    write(badGzip, badTrailer);
    rejectArchive(badGzip, "Gzip trailer CRC mismatch");
    for (const char* fixture : {"encrypted.zip", "encrypted.7z", "incomplete.part1.rar", "solid-rar4.rar"}) {
        const auto copy = inputDirectory / fixture;
        fs::copy_file(fixtures / fixture, copy);
        rejectArchive(copy, fixture);
    }

    const auto unsafe = inputDirectory / "unsafe.tar";
    makeArchive(unsafe, L"tar", {{"safe.txt", {EntryType::file, "first"}},
                                {"z/../../escape", {EntryType::file, "outside"}}});
    rejectArchive(unsafe, "Parent path traversal");
    expect(!fs::exists(root / "escape"), "An archive entry must never write outside its output");
    const auto absolute = inputDirectory / "absolute.tar";
    makeArchive(absolute, L"tar", {{"/absolute", {EntryType::file, "outside"}}});
    rejectArchive(absolute, "Absolute archive path");
    const auto symlink = inputDirectory / "symlink.tar";
    makeArchive(symlink, L"tar", {{"outside", {EntryType::symlink, "../outside"}}});
    rejectArchive(symlink, "Symbolic link entry");

    const auto partialBatch = ca::execute(job({broken, zip}, failureDirectory, L"tar.gz"), cancel, {});
    expect(partialBatch.succeeded == 1 && partialBatch.failed == 1 && partialBatch.outputs.size() == 1,
           "An unreadable archive does not stop a later archive in a batch");
    verifyArchive(partialBatch.outputs.at(0), L"tar.gz", standard);
    expect(std::distance(fs::directory_iterator(failureDirectory), fs::directory_iterator{}) == 1,
           "Only the successful batch result is published");

    const auto cancelOutput = root / "cancel";
    fs::create_directory(cancelOutput);
    const auto original = read(zip);
    cancel = true;
    const auto preCancelled = ca::execute(job({zip}, cancelOutput, L"tar.gz"), cancel, {});
    expect(preCancelled.cancelled && preCancelled.failed == 0 && preCancelled.outputs.empty() &&
           fs::is_empty(cancelOutput) && read(zip) == original, "Pre-cancelled repack leaves no result");
    cancel = false;
    const auto cancelled = ca::execute(job({zip}, cancelOutput, L"tar.gz"), cancel, [&](const ca::Update& update) {
        if (update.progress == 0) cancel = true;
    });
    expect(cancelled.cancelled && cancelled.failed == 0 && cancelled.outputs.empty() &&
           fs::is_empty(cancelOutput) && read(zip) == original, "Cancellation after the working update leaves no result");
    cancel = false;
    const auto cancelledBatch = ca::execute(job({zip, seven}, cancelOutput, L"tar.gz"), cancel,
                                             [&](const ca::Update& update) {
        if (!update.output.empty()) cancel = true;
    });
    expect(cancelledBatch.cancelled && cancelledBatch.succeeded == 1 && cancelledBatch.failed == 0 &&
           cancelledBatch.outputs.size() == 1 &&
           std::distance(fs::directory_iterator(cancelOutput), fs::directory_iterator{}) == 1,
           "Cancelling a batch keeps the first completed result and skips remaining archives");
    verifyArchive(cancelledBatch.outputs.at(0), L"tar.gz", standard);

    const auto inFlightOutput = root / "cancel-in-flight";
    fs::create_directory(inFlightOutput);
    const auto slowZip = inputDirectory / "cancel-in-flight.zip";
    std::string randomData(16 * 1024 * 1024, '\0');
    uint32_t state = 1;
    for (char& byte : randomData) {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        byte = static_cast<char>(state & 0xff);
    }
    makeArchive(slowZip, L"zip", {{"data.bin", {EntryType::file, std::move(randomData)}}});
    cancel = false;
    const auto inFlight = [&] {
        std::jthread watch([&](std::stop_token stop) {
            // Only the helper creates this file, so cancellation happens after it starts.
            while (!stop.stop_requested()) {
                std::error_code error;
                for (fs::directory_iterator it(inFlightOutput, error), end; !error && it != end; it.increment(error)) {
                    if (fs::exists(it->path() / "result.7z", error)) {
                        cancel = true;
                        return;
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
        return ca::execute(job({slowZip}, inFlightOutput, L"7z"), cancel, {});
    }();
    expect(inFlight.cancelled && inFlight.failed == 0 && inFlight.outputs.empty() && fs::is_empty(inFlightOutput),
           "Cancel the running helper and remove its incomplete output");
}
}

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
#else
int main(int argc, char** argv) {
#endif
    try {
        expect(argc == 3, "Pass an isolated test directory and the archive fixture directory");
#ifdef _WIN32
        expect(std::setlocale(LC_CTYPE, ".UTF-8") != nullptr, "Set Windows UTF-8 test locale");
#else
        expect(std::setlocale(LC_CTYPE, "C.UTF-8") || std::setlocale(LC_CTYPE, "en_US.UTF-8"),
               "Set POSIX UTF-8 test locale");
#endif
        TestDirectory root(fs::absolute(argv[1]));
#ifdef _WIN32
        expect(_wputenv_s(L"TEMP", root.path.c_str()) == 0 && _wputenv_s(L"TMP", root.path.c_str()) == 0,
               "Set workspace-local temporary directory");
#else
        expect(setenv("TMPDIR", root.path.c_str(), 1) == 0, "Set workspace-local temporary directory");
#endif
        runTests(root.path, fs::absolute(argv[2]));
        std::cout << "PASS: all supported archive pairs, entry data, path handling, headless batch, "
                     "conflicts, cancellation, malformed inputs and unsupported archive features\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
