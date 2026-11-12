#include "archive_platform.h"
#include <archive_entry.h>

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace ca::archiveTool {
namespace {
constexpr std::uint64_t maxExpandedBytes = 8 * gibibyte;
constexpr std::size_t maxEntries = 100000;
using Reader = std::unique_ptr<archive, decltype(&archive_read_free)>;
using Entry = std::unique_ptr<archive_entry, decltype(&archive_entry_free)>;
struct CloseFile {
    void operator()(std::FILE* file) const { std::fclose(file); }
};
using File = std::unique_ptr<std::FILE, CloseFile>;

void check(int status, archive* handle) {
    if (status == ARCHIVE_OK) return;
    const char* message = archive_error_string(handle);
    throw std::runtime_error(message ? message : "Archive operation failed");
}

struct GzipInput {
    gzFile file;
    std::array<char, 64 * 1024> buffer{};
    std::uint64_t remaining = maxContainerBytes;

    explicit GzipInput(const fs::path& path) : file(openGzip(path)) {
        if (!file) throw std::runtime_error("Cannot open gzip input");
    }
    ~GzipInput() { gzclose_r(file); }

    static la_ssize_t read(archive* handle, void* data, const void** contents) {
        auto& input = *static_cast<GzipInput*>(data);
        const int count = gzread(input.file, input.buffer.data(), static_cast<unsigned>(input.buffer.size()));
        *contents = input.buffer.data();
        int error = Z_OK;
        const char* message = gzerror(input.file, &error);
        if (count >= 0 && error == Z_OK) {
            const auto bytes = static_cast<std::uint64_t>(count);
            if (bytes > input.remaining) {
                archive_set_error(handle, EFBIG, "Expanded gzip stream exceeds the 9 GiB limit");
                return ARCHIVE_FATAL;
            }
            input.remaining -= bytes;
            return count;
        }
        archive_set_error(handle, error, "Invalid gzip data: %s", message ? message : "read failed");
        return ARCHIVE_FATAL;
    }
};

struct ArchiveOutput {
    File file;
    std::uint64_t remaining = maxContainerBytes;

    explicit ArchiveOutput(const fs::path& path) : file(openFile(path, true)) {
        if (!file) throw std::runtime_error("Cannot open output archive");
    }

    static la_ssize_t write(archive* handle, void* data, const void* contents, size_t size) {
        auto& output = *static_cast<ArchiveOutput*>(data);
        if (size > output.remaining) {
            archive_set_error(handle, EFBIG, "Archive output exceeds the 9 GiB limit");
            return -1;
        }
        if (std::fwrite(contents, 1, size, output.file.get()) != size) {
            archive_set_error(handle, errno, "Cannot write output archive");
            return -1;
        }
        output.remaining -= size;
        return static_cast<la_ssize_t>(size);
    }
};

unsigned readByte(std::FILE* input) {
    const int byte = std::fgetc(input);
    if (byte == EOF) throw std::runtime_error("Truncated RAR header");
    return static_cast<unsigned>(byte);
}

std::uint64_t readRarInteger(std::FILE* input) {
    std::uint64_t result = 0;
    for (unsigned shift = 0; shift < 64; shift += 7) {
        const auto byte = readByte(input);
        if (shift == 63 && byte > 1) break;
        result |= static_cast<std::uint64_t>(byte & 0x7f) << shift;
        if ((byte & 0x80) == 0) return result;
    }
    throw std::runtime_error("Invalid RAR header");
}

void checkRarVolume(std::FILE* input) {
    // libarchive can read individual RAR volumes. Refuse to publish an incomplete conversion.
    constexpr std::array<unsigned, 6> signature{'R', 'a', 'r', '!', 0x1a, 0x07};
    for (auto byte : signature) {
        if (readByte(input) != byte) throw std::runtime_error("Input is not a RAR archive");
    }
    std::uint64_t flags = 0;
    switch (readByte(input)) {
    case 0: {
        // RAR 4.x main header starts with the CRC16 and header type.
        if (std::fseek(input, 2, SEEK_CUR) != 0 || readByte(input) != 0x73)
            throw std::runtime_error("Invalid RAR main header");
        const auto lowFlags = readByte(input);
        flags = lowFlags | (readByte(input) << 8);
        if (flags & 0x80) throw std::runtime_error("Encrypted archives are unsupported");
        break;
    }
    case 1: {
        // RAR 5.x main header follows the signature, CRC32 and variable-length header size.
        if (readByte(input) != 0 || std::fseek(input, 4, SEEK_CUR) != 0)
            throw std::runtime_error("Invalid RAR signature");
        readRarInteger(input);
        const auto type = readRarInteger(input);
        if (type == 4) throw std::runtime_error("Encrypted archives are unsupported");
        if (type != 1) throw std::runtime_error("Invalid RAR main header");
        const auto headerFlags = readRarInteger(input);
        if (headerFlags & 1) readRarInteger(input); // Extra area size
        if (headerFlags & 2) readRarInteger(input); // Data area size
        flags = readRarInteger(input);
        break;
    }
    default: throw std::runtime_error("Unsupported RAR signature");
    }
    if (flags & 1) throw std::runtime_error("Multi-volume archives are unsupported");
    if (std::fseek(input, 0, SEEK_SET) != 0) throw std::runtime_error("Cannot rewind RAR input");
}

std::string entryPath(archive_entry* entry, bool directory) {
    const auto* raw = archive_entry_pathname_utf8(entry);
    if (!raw) throw std::runtime_error("Cannot read archive entry name as UTF-8");
    std::string_view remaining(raw);
    if (remaining.empty() || remaining.starts_with('/') || remaining.find_first_of("\\:") != remaining.npos)
        throw std::runtime_error("Unsafe archive entry path");
    std::string result;
    while (!remaining.empty()) {
        const auto separator = remaining.find('/');
        const auto component = remaining.substr(0, separator);
        if (component == "..") throw std::runtime_error("Unsafe archive entry path");
        if (!component.empty() && component != ".") {
            if (!result.empty()) result += '/';
            result += component;
        }
        if (separator == remaining.npos) break;
        remaining.remove_prefix(separator + 1);
    }
    if (result.empty() && !directory) throw std::runtime_error("Missing archive entry name");
    return result;
}

void copyEntry(archive* reader, archive* writer, archive_entry* source, std::uint64_t& expandedRemaining) {
    if (archive_entry_is_encrypted(source)) throw std::runtime_error("Encrypted archives are unsupported");
    const auto type = archive_entry_filetype(source);
    if ((type != AE_IFREG && type != AE_IFDIR) || archive_entry_hardlink(source) || archive_entry_symlink(source))
        throw std::runtime_error("Archive links and special files are unsupported");
    const auto name = entryPath(source, type == AE_IFDIR);
    const auto size = archive_entry_size(source);
    if (size < 0 || (type == AE_IFREG && !archive_entry_size_is_set(source)) || (type == AE_IFDIR && size != 0))
        throw std::runtime_error("Unknown archive entry size");
    if (name.empty()) return; // Ignore the optional root directory marker after checking its size.
    const auto entryBytes = static_cast<std::uint64_t>(size);
    if (entryBytes > expandedRemaining) throw std::runtime_error("Archive contents exceed the 8 GiB expanded size limit");
    // The per-entry copy below refuses bytes beyond this reservation.
    expandedRemaining -= entryBytes;

    Entry entry(archive_entry_new(), archive_entry_free);
    if (!entry) throw std::bad_alloc();
    archive_entry_set_pathname_utf8(entry.get(), name.c_str());
    archive_entry_set_filetype(entry.get(), type);
    archive_entry_set_perm(entry.get(), archive_entry_perm(source) & 0777);
    archive_entry_set_size(entry.get(), size);
    if (archive_entry_mtime_is_set(source))
        archive_entry_set_mtime(entry.get(), archive_entry_mtime(source), archive_entry_mtime_nsec(source));
    check(archive_write_header(writer, entry.get()), writer);
    if (type == AE_IFDIR) {
        check(archive_write_finish_entry(writer), writer);
        return;
    }

    std::array<char, 64 * 1024> buffer;
    auto remaining = size;
    while (true) {
        const auto count = archive_read_data(reader, buffer.data(), buffer.size());
        if (count < 0) check(static_cast<int>(count), reader);
        if (count == 0) break;
        if (count > remaining) throw std::runtime_error("Archive entry exceeds its declared size");
        const auto written = archive_write_data(writer, buffer.data(), static_cast<size_t>(count));
        if (written < 0) check(static_cast<int>(written), writer);
        if (written != count) throw std::runtime_error("Cannot write full archive entry");
        remaining -= count;
    }
    if (remaining != 0) throw std::runtime_error("Truncated archive entry");
    check(archive_write_finish_entry(writer), writer);
}

}

void repack(const fs::path& from, const fs::path& to, const fs::path& source, const fs::path& destination) {
    std::unique_ptr<GzipInput> gzip;
    File rar;
    std::unique_ptr<ArchiveOutput> output;
    Reader reader(archive_read_new(), archive_read_free);
    const auto freeWriter = [](archive* handle) {
        // The successful path closes explicitly. On failure, skip finalizing the partial archive.
        archive_write_fail(handle);
        archive_write_free(handle);
    };
    std::unique_ptr<archive, decltype(freeWriter)> writer(archive_write_new(), freeWriter);
    if (!reader || !writer) throw std::bad_alloc();

    if (from == "zip") check(archive_read_support_format_zip_seekable(reader.get()), reader.get());
    else if (from == "tar" || from == "tar.gz") {
        check(archive_read_support_format_tar(reader.get()), reader.get());
        check(archive_read_set_format_option(reader.get(), "tar", "read_concatenated_archives", "1"), reader.get());
    } else if (from == "7z") check(archive_read_support_format_7zip(reader.get()), reader.get());
    else if (from == "rar") {
        check(archive_read_support_format_rar(reader.get()), reader.get());
        check(archive_read_support_format_rar5(reader.get()), reader.get());
    } else throw std::runtime_error("Unsupported input archive format");

    if (from == "tar.gz") {
        // Use zlib for input so the gzip CRC and size trailer are checked as data is read.
        gzip = std::make_unique<GzipInput>(source);
        if (gzdirect(gzip->file)) throw std::runtime_error("Input is not gzip compressed");
        check(archive_read_open(reader.get(), gzip.get(), nullptr, GzipInput::read, nullptr), reader.get());
    } else if (from == "rar") {
        rar.reset(openFile(source, false));
        if (!rar) throw std::runtime_error("Cannot open RAR input");
        checkRarVolume(rar.get());
        check(archive_read_open_FILE(reader.get(), rar.get()), reader.get());
    } else {
        check(openReader(reader.get(), source), reader.get());
    }

    if (to == "zip") {
        check(archive_write_set_format_zip(writer.get()), writer.get());
        check(archive_write_set_format_option(writer.get(), "zip", "hdrcharset", "UTF-8"), writer.get());
    } else if (to == "tar.gz") {
        check(archive_write_set_format_pax_restricted(writer.get()), writer.get());
        check(archive_write_add_filter_gzip(writer.get()), writer.get());
    } else if (to == "7z") check(archive_write_set_format_7zip(writer.get()), writer.get());
    else throw std::runtime_error("Unsupported output archive format");
    check(archive_write_set_bytes_in_last_block(writer.get(), 1), writer.get());
    output = std::make_unique<ArchiveOutput>(destination);
    check(archive_write_open2(writer.get(), output.get(), nullptr, ArchiveOutput::write, nullptr, nullptr), writer.get());

    std::size_t entries = 0;
    auto expandedRemaining = maxExpandedBytes;
    for (archive_entry* entry = nullptr;;) {
        const auto status = archive_read_next_header(reader.get(), &entry);
        if (status == ARCHIVE_EOF) break;
        check(status, reader.get());
        if (++entries > maxEntries) throw std::runtime_error("Archive exceeds the 100,000 entry limit");
        copyEntry(reader.get(), writer.get(), entry, expandedRemaining);
    }
    if (gzip) {
        // The tar reader can stop at its end marker before zlib has read the gzip trailer.
        const void* ignored = nullptr;
        la_ssize_t count;
        while ((count = GzipInput::read(reader.get(), gzip.get(), &ignored)) > 0) {}
        if (count < 0) check(static_cast<int>(count), reader.get());
    }
    check(archive_read_close(reader.get()), reader.get());
    check(archive_write_close(writer.get()), writer.get());
    if (std::fclose(output->file.release()) != 0) throw std::runtime_error("Cannot close output archive");
}
}
