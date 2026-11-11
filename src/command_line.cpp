#include "core.h"
#include <fstream>
#include <iostream>

namespace ca {
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
}

std::optional<int> runCommandLine(const CommandLine& command) {
    if (command.help) {
        std::cout << "Context Actions [options] [--] [files...]\n"
                  << "  --action compress|h264|h265|telegram|image|pdf|rename|word|excel\n"
                  << "  --headless --report PATH    Run a batch and write a report\n"
                  << "  --run                      Start the batch when the window opens\n"
                  << "  --output DIRECTORY         Use a separate output directory\n"
                  << "  --quality N --width N --fps N --target-mb N --mute\n"
                  << "  --format png|jpg|webp|bmp|tiff|gif --first-frame\n"
                  << "  --pattern TEXT --prefix TEXT --suffix TEXT --find TEXT --replace TEXT --start N\n";
        return 0;
    }
    if (!command.headless) return std::nullopt;
    if (command.report.empty()) throw std::runtime_error("--headless requires --report");
    std::ofstream log(command.report, std::ios::binary | std::ios::trunc);
    if (!log) throw std::runtime_error("Cannot open report");
    std::atomic_bool cancel = false;
    const auto summary = execute(command.options, cancel, [&](const Update& update) {
        log << update.index << '\t' << utf8(update.status) << '\t' << utf8(pathText(update.output)) << '\n';
    });
    log << "success=" << summary.succeeded << " failed=" << summary.failed << " cancelled=" << summary.cancelled << '\n';
    log.flush();
    if (!log) throw std::runtime_error("Cannot write report");
    return summary.cancelled ? 2 : summary.failed ? 1 : 0;
}

void reportCommandError(const CommandLine& command, const std::exception& error) {
    if (command.headless && !command.report.empty()) {
        std::ofstream log(command.report, std::ios::binary | std::ios::app);
        log << "error=" << error.what() << '\n';
        if (log) return;
    }
    std::cerr << "Context Actions: " << error.what() << '\n';
}
}
