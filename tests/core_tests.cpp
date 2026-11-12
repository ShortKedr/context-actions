#include "platform.h"
#ifdef _WIN32
#include "windows/platform_win.h"
#include <shellapi.h>
#else
#include <cstdlib>
#include <unistd.h>
#endif
#include <algorithm>
#include <chrono>
#include <thread>
#include <fstream>
#include <iostream>
#include <iterator>

namespace {
void expect(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F fn){bool rejected=false;try{fn();}catch(const std::exception&){rejected=true;}expect(rejected,"Expected rejection");}
void write(const ca::fs::path& file,const char* data){std::ofstream out(file,std::ios::binary);out<<data;}
std::string read(const ca::fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
#ifndef _WIN32
bool isPrivate(const ca::fs::path& path) {
    using ca::fs::perms;
    return (ca::fs::status(path).permissions() & (perms::group_all | perms::others_all)) == perms::none;
}
#endif
}
int runTests(const ca::fs::path& self, const ca::fs::path& root) {
    try {
        ca::fs::create_directories(root);
        auto state = root / "state";
#ifdef _WIN32
        SetEnvironmentVariableW(L"CONTEXT_ACTIONS_DATA_DIR", state.c_str());
        auto scratch = root / std::to_string(GetCurrentProcessId());
#else
        setenv("CONTEXT_ACTIONS_DATA_DIR", state.c_str(), 1);
        auto scratch = root / std::to_string(getpid());
        ca::fs::create_directories(state);
        ca::fs::permissions(state, ca::fs::perms::owner_all | ca::fs::perms::group_read | ca::fs::perms::group_exec);
#endif
        ca::setLanguage(L"en");
#ifndef _WIN32
        expect(isPrivate(state), "Existing POSIX state directory becomes private");
#endif
        ca::fs::create_directories(scratch);
        expect(ca::kind(ca::textPath(L"Кино.MKV"))==ca::Kind::video,"File classification");
        expect(ca::accepts(ca::Action::h264,{L"a.mp4",L"b.mov"}),"Video batch");
        expect(!ca::accepts(ca::Action::h264,{L"a.mp4",L"b.png"}),"Mixed video rejection");
        expect(ca::accepts(ca::Action::pdf,{L"a.pdf",L"b.png"}),"Mixed PDF batch");
        expect(!ca::unavailableReason(ca::Action::sheets,{L"a.xls"}).empty(),"Deferred engine is visible and unavailable");
        #ifdef _WIN32
        std::vector<std::wstring> arguments{L"C:\\Program Files\\app.exe",L"",L"path with spaces\\",L"quotes\"here",L"Кириллица & % ! $ ` (тест).mp4",L"back\\\"quote"};
        std::wstring line;for(const auto& a:arguments)line+=ca::quote(a)+L" ";int parsedCount=0;auto parsed=CommandLineToArgvW(line.c_str(),&parsedCount);
        expect(parsedCount==arguments.size(),"Quoted argument count");for(int i=0;i<parsedCount;++i)expect(parsed[i]==arguments[i],"Windows argument quoting");LocalFree(parsed);
        #endif
        ca::Options options;options.action=ca::Action::h265;options.files={scratch/ca::textPath(L"кино & 100%.mp4")};options.pattern=L"{name}_{n}";options.output=scratch;
        auto job=ca::writeJob(options);auto restored=ca::readJob(job);expect(restored.files==options.files&&restored.action==options.action,"Job round trip");ca::consumeJob(job);expect(!ca::fs::exists(job),"Internal job consumed");
        auto external=scratch/L"external.caj";ca::saveOptions(options,external);ca::consumeJob(external);expect(ca::fs::exists(external),"External job retained");
        auto corrupt=scratch/L"corrupt.caj";write(corrupt,"CAJ1\xFF\xFF\xFF\xFF");rejects([&]{ca::readJob(corrupt);});
        auto args=ca::videoArguments(options,L"input",L"output");expect(std::find(args.begin(),args.end(),L"libx265")!=args.end(),"Software HEVC encoder");
        const auto formatLimit = std::find(args.begin(), args.end(), L"-format_whitelist");
        const auto protocolLimit = std::find(args.begin(), args.end(), L"-protocol_whitelist");
        const auto input = std::find(args.begin(), args.end(), L"-i");
        expect(input != args.end() && formatLimit < input && protocolLimit < input && *(protocolLimit + 1) == L"file" &&
               (L"," + *(formatLimit + 1) + L",").find(L",concat,") == std::wstring::npos &&
               (L"," + *(formatLimit + 1) + L",").find(L",hls,") == std::wstring::npos,
               "Media inputs restrict protocols and manifest demuxers before opening the file");
        options.action=ca::Action::telegram;args=ca::videoArguments(options,L"input",L"output");expect(std::find(args.begin(),args.end(),L"-an")!=args.end(),"Silent Telegram animation");expect(std::find(args.begin(),args.end(),L"libx264")!=args.end(),"Telegram H264");
        options.targetMB=2;rejects([&]{ca::videoArguments(options,L"in",L"out",0);});args=ca::videoArguments(options,L"in",L"out",10);expect(std::find(args.begin(),args.end(),L"-crf")==args.end(),"Size mode is bitrate based");
        write(scratch/L"source.txt","original");write(scratch/L"target.txt","existing");rejects([&]{ca::commitOutput(scratch/L"source.txt",scratch/L"target.txt");});expect(ca::fs::file_size(scratch/L"target.txt")==8,"No overwrite");
        options=ca::Options{};options.action=ca::Action::rename;options.files={scratch/L"source.txt"};options.pattern=L"{name}_{n}";
        auto retained=options;retained.width=1;retained.fps=-1;retained.quality=-1;retained.targetMB=-1;retained.output=options.files[0];retained.format=L"invalid";
        ca::validate(retained); // Hidden conversion fields must not block rename after switching actions.
        retained.start=-1;rejects([&]{ca::validate(retained);});
        retained=options;retained.action=ca::Action::h264;retained.files={scratch/L"input.mp4"};retained.start=-1;ca::validate(retained);
        retained.width=1;rejects([&]{ca::validate(retained);});retained.width=0;retained.fps=-1;rejects([&]{ca::validate(retained);});
        retained.action=ca::Action::image;retained.files={scratch/L"input.png"};ca::validate(retained);
        retained.quality=52;rejects([&]{ca::validate(retained);});retained.quality=24;retained.output=options.files[0];rejects([&]{ca::validate(retained);});
        auto changes=ca::previewRename(options);expect(changes[0].to.filename()==L"source_1.txt","Rename pattern");
        std::atomic_bool cancel=true;rejects([&]{ca::applyRename(changes,cancel);});expect(ca::fs::exists(options.files[0]),"Cancelled rename preserves source");cancel=false;
        ca::Options missing;missing.action=ca::Action::image;missing.files={scratch/L"missing.png"};std::vector<ca::Update> updates;
        auto failed=ca::execute(missing,cancel,[&](auto const& update){updates.push_back(update);});
        expect(failed.failed==1&&updates.size()==1&&updates[0].failed&&updates[0].status.find(L"missing.png")!=std::wstring::npos,"Typed failure preserves diagnostic details");
        ca::CommandLine report;
        report.headless = true;
        report.options = options;
        report.report = options.files[0];
        rejects([&] { ca::runCommandLine(report); });
        expect(read(options.files[0]) == "original", "A report cannot truncate an input");
#ifndef _WIN32
        report.report = scratch / "linked-report.txt";
        ca::fs::create_hard_link(options.files[0], report.report);
        rejects([&] { ca::runCommandLine(report); });
        expect(read(options.files[0]) == "original", "A report cannot truncate a hard-linked input");
        report.report = scratch / "symlink-report.txt";
        ca::fs::create_symlink(options.files[0], report.report);
        rejects([&] { ca::runCommandLine(report); });
        expect(read(options.files[0]) == "original", "A report cannot truncate a symlink target");
#endif
        report.options = missing;
        report.report = scratch / "new-report.txt";
        expect(ca::runCommandLine(report) == 1 && read(report.report).find("failed=1") != std::string::npos,
               "New report records conversion failure");
#ifndef _WIN32
        expect(isPrivate(report.report), "POSIX report is private");
        auto image = missing;
        image.files = {scratch / "image.png"};
        write(image.files.front(), "image fixture");
        bool checkedScratch = false;
        const auto imageResult = ca::execute(image, cancel, [&](const ca::Update&) {
            if (checkedScratch) return;
            expect(ca::fs::canonical(ca::conversionScratchRoot()) != ca::fs::canonical(scratch),
                   "Conversions do not stage under the output directory");
            for (const auto& item : ca::fs::directory_iterator(ca::conversionScratchRoot())) {
                if (item.path().filename().native().starts_with(".context-actions-")) {
                    expect(isPrivate(item.path()), "POSIX conversion scratch directory is private");
                    checkedScratch = true;
                }
            }
            throw ca::Cancelled();
        });
        expect(imageResult.cancelled && checkedScratch, "Conversion scratch permissions checked before processing");
#endif
        const auto previousReport = read(report.report);
        rejects([&] { ca::runCommandLine(report); });
        expect(read(report.report) == previousReport, "An existing report is preserved");
        cancel=true;updates.clear();auto stopped=ca::execute(missing,cancel,[&](auto const& update){updates.push_back(update);});
        expect(stopped.cancelled&&updates.size()==1&&!updates[0].failed,"Cancellation is not an error detail");cancel=false;
        ca::applyRename(changes,cancel);expect(ca::fs::exists(changes[0].to),"Rename executed");
#ifndef _WIN32
        expect(isPrivate(state / "rename-undo.txt"), "POSIX rename journal is private");
#endif
        ca::undoRename(cancel);expect(ca::fs::exists(options.files[0]),"Rename undo");
        options.pattern=L"target";rejects([&]{ca::previewRename(options);});options.pattern=L"../escape";rejects([&]{ca::previewRename(options);});options.pattern=L"CON";rejects([&]{ca::previewRename(options);});
        ca::resetOfficeCapabilities();expect(ca::officeCapability(true)==ca::officeCapability(false),"Office starts with the same capabilities");const auto sheets=ca::officeCapability(true);ca::setOfficeCapability(false,ca::Capability::unavailable);expect(ca::officeCapability(true)==sheets,"Independent spreadsheet PDF capability");expect(!ca::unavailableReason(ca::Action::word,{L"a.docx"}).empty(),"Missing Office shown");
        for(auto lang:{L"en",L"ru",L"de",L"fr",L"zh-CN"}){ca::setLanguage(lang);for(auto a:ca::catalog())expect(ca::trAction(a)!=ca::actionId(a),"Action localization");}
        cancel=false;rejects([&]{ca::runProcess(self,{L"--child"},cancel,[&](const auto&){cancel=true;});});expect(cancel,"Child process cancellation");
        cancel=false;
        const auto echo = ca::runProcess(self,{L"--echo",L"Unicode é & % ! $ ` (test) \""},cancel);
        expect(echo.code==0 && echo.output=="Unicode \xc3\xa9 & % ! $ ` (test) \"", "Child argument stays literal");
        rejects([&]{ca::runProcess(self,{L"--child"},cancel,{},30);});
        expect(!cancel,"Process timeout keeps the cancellation flag unchanged");
#ifdef __APPLE__
        rejects([&]{ca::runProcess(self,{L"--child"},cancel,{},10000,1);});
#endif
        ca::CommandLine command;
        ca::parseArguments({L"--action",L"rename",L"--pattern",L"{name}_{n}",L"--",L"--literal"},command);
        expect(command.options.action==ca::Action::rename && command.options.files[0].filename()=="--literal","CLI supports option-like filenames");
        std::cout<<"PASS: classification, selection, arguments, bounded jobs, presets, action-specific validation, no overwrite, rename/undo, typed failure details, Office state, five languages, child cancellation\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}


#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    const auto argument = argc == 2 ? std::wstring(argv[1]) : std::wstring();
#else
int main(int argc, char** argv) {
    const auto argument = argc == 2 ? ca::wide(argv[1]) : std::wstring();
#endif
    if (argument == L"--child") {
        std::cout << "ready\n" << std::flush;
        std::this_thread::sleep_for(std::chrono::seconds(5));
        return 0;
    }
    if (argc == 3) {
#ifdef _WIN32
        if (std::wstring(argv[1]) != L"--echo") return 1;
        std::cout << ca::utf8(argv[2]);
#else
        if (std::string(argv[1]) != "--echo") return 1;
        std::cout << argv[2];
#endif
        return 0;
    }
    if (argument.empty()) {
        std::cerr << "Pass an isolated test directory.\n";
        return 1;
    }
    return runTests(ca::fs::absolute(argv[0]), ca::fs::absolute(ca::textPath(argument)));
}
