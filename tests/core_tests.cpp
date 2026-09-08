#include "core.h"
#include <shellapi.h>
#include <fstream>
#include <iostream>

namespace {
void expect(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F fn){bool rejected=false;try{fn();}catch(const std::exception&){rejected=true;}expect(rejected,"Expected rejection");}
void write(const ca::fs::path& file,const char* data){std::ofstream out(file,std::ios::binary);out<<data;}
}
int wmain(int argc,wchar_t** argv){
    if(argc==2&&std::wstring(argv[1])==L"--child"){std::cout<<"ready\n"<<std::flush;Sleep(5000);return 0;}
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    try {
        expect(argc==2,"Pass isolated test directory");ca::fs::path root=ca::fs::absolute(argv[1]);ca::fs::create_directories(root);
        SetEnvironmentVariableW(L"CONTEXT_ACTIONS_DATA_DIR",(root/L"state").c_str());ca::setLanguage(L"en");
        auto scratch=root/std::to_wstring(GetCurrentProcessId());ca::fs::create_directories(scratch);
        expect(ca::kind(L"Кино.MKV")==ca::Kind::video,"File classification");
        expect(ca::accepts(ca::Action::h264,{L"a.mp4",L"b.mov"}),"Video batch");
        expect(!ca::accepts(ca::Action::h264,{L"a.mp4",L"b.png"}),"Mixed video rejection");
        expect(ca::accepts(ca::Action::pdf,{L"a.pdf",L"b.png"}),"Mixed PDF batch");
        expect(!ca::unavailableReason(ca::Action::sheets,{L"a.xls"}).empty(),"Deferred engine is visible and unavailable");
        std::vector<std::wstring> arguments{L"C:\\Program Files\\app.exe",L"",L"path with spaces\\",L"quotes\"here",L"Кириллица & % ! $ ` (тест).mp4",L"back\\\"quote"};
        std::wstring line;for(const auto& a:arguments)line+=ca::quote(a)+L" ";int parsedCount=0;auto parsed=CommandLineToArgvW(line.c_str(),&parsedCount);
        expect(parsedCount==arguments.size(),"Quoted argument count");for(int i=0;i<parsedCount;++i)expect(parsed[i]==arguments[i],"Windows argument quoting");LocalFree(parsed);
        ca::Options options;options.action=ca::Action::h265;options.files={scratch/L"кино & 100%.mp4"};options.pattern=L"{name}_{n}";options.output=scratch;
        auto job=ca::writeJob(options);auto restored=ca::readJob(job);expect(restored.files==options.files&&restored.action==options.action,"Job round trip");ca::consumeJob(job);expect(!ca::fs::exists(job),"Internal job consumed");
        auto external=scratch/L"external.caj";ca::saveOptions(options,external);ca::consumeJob(external);expect(ca::fs::exists(external),"External job retained");
        auto corrupt=scratch/L"corrupt.caj";write(corrupt,"CAJ1\xFF\xFF\xFF\xFF");rejects([&]{ca::readJob(corrupt);});
        auto args=ca::videoArguments(options,L"input",L"output");expect(std::find(args.begin(),args.end(),L"libx265")!=args.end(),"Software HEVC encoder");
        options.action=ca::Action::telegram;args=ca::videoArguments(options,L"input",L"output");expect(std::find(args.begin(),args.end(),L"-an")!=args.end(),"Silent Telegram animation");expect(std::find(args.begin(),args.end(),L"libx264")!=args.end(),"Telegram H264");
        options.targetMB=2;rejects([&]{ca::videoArguments(options,L"in",L"out",0);});args=ca::videoArguments(options,L"in",L"out",10);expect(std::find(args.begin(),args.end(),L"-crf")==args.end(),"Size mode is bitrate based");
        write(scratch/L"source.txt","original");write(scratch/L"target.txt","existing");rejects([&]{ca::commitOutput(scratch/L"source.txt",scratch/L"target.txt");});expect(ca::fs::file_size(scratch/L"target.txt")==8,"No overwrite");
        options=ca::Options{};options.action=ca::Action::rename;options.files={scratch/L"source.txt"};options.pattern=L"{name}_{n}";
        auto changes=ca::previewRename(options);expect(changes[0].to.filename()==L"source_1.txt","Rename pattern");
        std::atomic_bool cancel=true;rejects([&]{ca::applyRename(changes,cancel);});expect(ca::fs::exists(options.files[0]),"Cancelled rename preserves source");cancel=false;
        ca::applyRename(changes,cancel);expect(ca::fs::exists(changes[0].to),"Rename executed");ca::undoRename(cancel);expect(ca::fs::exists(options.files[0]),"Rename undo");
        options.pattern=L"target";rejects([&]{ca::previewRename(options);});options.pattern=L"../escape";rejects([&]{ca::previewRename(options);});options.pattern=L"CON";rejects([&]{ca::previewRename(options);});
        ca::resetOfficeCapabilities();expect(ca::officeCapability(false)==ca::Capability::unknown,"Unknown Word");ca::setOfficeCapability(false,ca::Capability::unavailable);expect(ca::officeCapability(true)==ca::Capability::unknown,"Independent Excel");expect(!ca::unavailableReason(ca::Action::word,{L"a.docx"}).empty(),"Missing Office shown");
        for(auto lang:{L"en",L"ru",L"de",L"fr",L"zh-CN"}){ca::setLanguage(lang);for(auto a:ca::catalog())expect(ca::tr(ca::actionKey(a))!=ca::actionId(a),"Action localization");}
        cancel=false;rejects([&]{ca::runProcess(ca::appDirectory()/L"actions_tests.exe",{L"--child"},cancel,[&](const auto&){cancel=true;});});expect(cancel,"Child process cancellation");
        ca::runUi(ca::Options{},false,true);
        std::cout<<"PASS: classification, selection, Windows quoting, bounded jobs, presets, no overwrite, rename/undo, Office state, five languages, child cancellation, hidden native UI\n";
        CoUninitialize();return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';CoUninitialize();return 1;}
}
