#include "core.h"
#include <shellapi.h>
#include <fstream>

int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int) {
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32|LOAD_LIBRARY_SEARCH_APPLICATION_DIR);
    const auto apartment=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    int result=1;bool headless=false;ca::fs::path report;
    try {
        ca::check(apartment);
        int argc=0;auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);
        struct Args {LPWSTR* value;~Args(){LocalFree(value);}} owner{argv};
        ca::Options options;bool immediate=false;
        for(int i=1;i<argc;++i) {
            std::wstring arg=argv[i];
            auto value=[&]()->std::wstring{if(++i>=argc)throw std::runtime_error("Missing argument");return argv[i];};
            auto integer=[&](){auto s=value();size_t used=0;int n=std::stoi(s,&used);if(used!=s.size())throw std::runtime_error("Invalid number");return n;};
            if(arg==L"--") {while(++i<argc)options.files.emplace_back(ca::fs::absolute(argv[i]));break;}
            if(arg==L"--headless")headless=true;
            else if(arg==L"--run")immediate=true;
            else if(arg==L"--report")report=value();
            else if(arg==L"--job"){auto file=ca::fs::path(value());options=ca::readJob(file);ca::consumeJob(file);}
            else if(arg==L"--action")options.action=ca::parseAction(value());
            else if(arg==L"--output")options.output=ca::fs::absolute(value());
            else if(arg==L"--quality")options.quality=integer();
            else if(arg==L"--width")options.width=integer();
            else if(arg==L"--fps")options.fps=integer();
            else if(arg==L"--target-mb")options.targetMB=integer();
            else if(arg==L"--format")options.format=value();
            else if(arg==L"--pattern")options.pattern=value();
            else if(arg==L"--prefix")options.prefix=value();
            else if(arg==L"--suffix")options.suffix=value();
            else if(arg==L"--find")options.find=value();
            else if(arg==L"--replace")options.replace=value();
            else if(arg==L"--start")options.start=integer();
            else if(arg==L"--mute")options.mute=true;
            else if(arg==L"--first-frame")options.firstFrame=true;
            else if(arg.starts_with(L"--"))throw std::runtime_error("Unknown option: "+ca::utf8(arg));
            else options.files.emplace_back(ca::fs::absolute(arg));
        }
        if(headless) {
            if(report.empty())throw std::runtime_error("--headless requires --report");
            std::ofstream log(report,std::ios::binary|std::ios::trunc);
            if(!log)throw std::runtime_error("Cannot open report");
            std::atomic_bool cancel=false;
            auto summary=ca::execute(options,cancel,[&](const ca::Update& u){log<<u.index<<'\t'<<ca::utf8(u.status)<<'\t'<<ca::utf8(u.output.wstring())<<'\n';});
            log<<"success="<<summary.succeeded<<" failed="<<summary.failed<<" cancelled="<<summary.cancelled<<'\n';
            result=summary.cancelled?2:summary.failed?1:0;
        } else result=ca::runUi(options,immediate);
    } catch(const std::exception& e) {
        if(headless) {if(!report.empty()){std::ofstream log(report,std::ios::binary|std::ios::app);log<<"error="<<e.what()<<'\n';}}
        else MessageBoxW(nullptr,ca::wide(e.what()).c_str(),L"Context Actions",MB_OK|MB_ICONERROR);
    }
    if(SUCCEEDED(apartment))CoUninitialize();
    return result;
}
