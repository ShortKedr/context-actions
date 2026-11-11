#include "platform.h"
#include <iostream>

namespace ca {
namespace {
void writeReport(std::FILE* report, const std::string& text) {
    if (std::fwrite(text.data(), 1, text.size(), report) != text.size())
        throw std::runtime_error("Cannot write report");
}
}

void parseArguments(const std::vector<std::wstring>& arguments, CommandLine& command) {
    auto& options = command.options;
    for (size_t i = 0; i < arguments.size(); ++i) {
        const auto& arg = arguments[i];
        auto value = [&]() -> const std::wstring& {
            if (++i >= arguments.size()) throw std::runtime_error("Missing argument for " + utf8(arg));
            return arguments[i];
        };
        auto integer = [&]() {
            const auto& text = value();
            size_t used = 0;
            const int number = std::stoi(text, &used);
            if (used != text.size()) throw std::runtime_error("Invalid number for " + utf8(arg));
            return number;
        };
        if (arg == L"--") {
            while (++i < arguments.size()) options.files.push_back(fs::absolute(textPath(arguments[i])));
            break;
        }
        if (arg == L"--headless") command.headless = true;
        else if (arg == L"--help" || arg == L"-h") command.help = true;
        else if (arg == L"--run") command.immediate = true;
        else if (arg == L"--report") command.report = textPath(value());
        else if (arg == L"--job") {
            const auto file = textPath(value());
            options = readJob(file);
            consumeJob(file);
        }
        else if (arg == L"--action") options.action = parseAction(value());
        else if (arg == L"--output") options.output = fs::absolute(textPath(value()));
        else if (arg == L"--quality") options.quality = integer();
        else if (arg == L"--width") options.width = integer();
        else if (arg == L"--fps") options.fps = integer();
        else if (arg == L"--target-mb") options.targetMB = integer();
        else if (arg == L"--format") options.format = value();
        else if (arg == L"--pattern") options.pattern = value();
        else if (arg == L"--prefix") options.prefix = value();
        else if (arg == L"--suffix") options.suffix = value();
        else if (arg == L"--find") options.find = value();
        else if (arg == L"--replace") options.replace = value();
        else if (arg == L"--start") options.start = integer();
        else if (arg == L"--mute") options.mute = true;
        else if (arg == L"--first-frame") options.firstFrame = true;
        else if (arg.starts_with(L"--")) throw std::runtime_error("Unknown option: " + utf8(arg));
        else options.files.push_back(fs::absolute(textPath(arg)));
    }
    if (command.immediate && !command.headless && !command.help && options.action == Action::repack)
        validate(options);
}

std::optional<int> runCommandLine(const CommandLine& command) {
    if (command.help) {
        std::cout << "Context Actions [options] [--] [files...]\n"
                  << "  --action compress|h264|h265|telegram|image|pdf|rename|word|excel\n"
                  << "           audio|remux|frame|media-info|repack\n"
                  << "  --headless --report PATH    Run a batch and write a new report\n"
                  << "  --run                      Start the batch when the window opens\n"
                  << "  --output DIRECTORY         Use a separate output directory\n"
                  << "  --quality N --width N --fps N --target-mb N --mute\n"
                  << "  --format png|jpg|webp|bmp|tiff|gif for image, --first-frame for image or PDF\n"
                  << "  --format mp3|m4a|wav|flac|opus for audio, mkv|mp4|mov for remux\n"
                  << "  --format zip|tar.gz|7z for archive repack\n"
                  << "  --pattern TEXT --prefix TEXT --suffix TEXT --find TEXT --replace TEXT --start N\n";
        return 0;
    }
    if (!command.headless) return std::nullopt;
    if (command.report.empty()) throw std::runtime_error("--headless requires --report");
    auto log = openNewPrivateFile(command.report);
    std::atomic_bool cancel = false;
    Result summary;
    try {
        summary = execute(command.options, cancel, [&](const Update& update) {
            writeReport(log.get(), std::to_string(update.index) + '\t' + utf8(update.status) + '\t' +
                                  utf8(pathText(update.output)) + '\n');
        });
    } catch (const std::exception& error) {
        writeReport(log.get(), std::string("error=") + error.what() + '\n');
        if (std::fclose(log.release()) != 0) throw std::runtime_error("Cannot write report");
        throw;
    }
    writeReport(log.get(), "success=" + std::to_string(summary.succeeded) + " failed=" +
                          std::to_string(summary.failed) + " cancelled=" + std::to_string(summary.cancelled) + '\n');
    if (std::fclose(log.release()) != 0) throw std::runtime_error("Cannot write report");
    return summary.cancelled ? 2 : summary.failed ? 1 : 0;
}

void reportCommandError(const CommandLine& command, const std::exception& error) {
    if (command.headless && !command.report.empty()) {
        try {
            auto log = openNewPrivateFile(command.report);
            writeReport(log.get(), std::string("error=") + error.what() + '\n');
            if (std::fclose(log.release()) == 0) return;
        } catch (const std::exception&) {
            // If the report is unavailable or already exists, fall back to stderr.
        }
    }
    std::cerr << "Context Actions: " << error.what() << '\n';
}
}
