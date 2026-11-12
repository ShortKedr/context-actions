#include "windows/platform_win.h"
#include <shlobj.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <thread>

namespace {
void expect(bool value,const char* message){if(!value)throw std::runtime_error(message);}
const CLSID commandClsid={0xa836fb4f,0x6548,0x4fc3,{0x8d,0x67,0x50,0x26,0x7b,0x25,0xa4,0x18}};
const CLSID classicClsid={0xee9d6c0b,0x0a19,0x4f8c,{0x8e,0x61,0xc7,0x8d,0xd8,0x81,0x6d,0xb0}};
ca::ComPtr<IShellItemArray> selection(ca::fs::path const& root,std::initializer_list<const wchar_t*> names){
    struct Ids {std::vector<PIDLIST_ABSOLUTE> values;~Ids(){for(auto id:values)CoTaskMemFree(id);}} ids;
    for(auto name:names){auto path=root/name;std::ofstream(path)<<"disposable shell selection";PIDLIST_ABSOLUTE id{};ca::check(SHParseDisplayName(path.c_str(),nullptr,&id,0,nullptr));ids.values.push_back(id);}
    ca::ComPtr<IShellItemArray> result;
    ca::check(SHCreateShellItemArrayFromIDLists(static_cast<UINT>(ids.values.size()),const_cast<PCIDLIST_ABSOLUTE*>(ids.values.data()),&result));return result;
}

int receiveLaunch(int argc, wchar_t** argv) {
    try {
        ca::CommandLine command;
        ca::parseArguments(std::vector<std::wstring>(argv + 1, argv + argc), command);
        expect(!command.immediate && !command.headless, "Shell launch must wait for Run");
        const auto receipt = ca::dataDirectory() / (command.chooseAction ? L"chooser.caj" : L"direct.caj");
        const auto pending = receipt.wstring() + L".pending";
        ca::saveOptions(command.options, pending);
        ca::fs::rename(pending, receipt);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

void expectLaunch(auto invoke, const ca::fs::path& receipt,
                  ca::Action action, const std::vector<ca::fs::path>& files) {
    ca::fs::remove(receipt);
    ca::check(invoke());
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!ca::fs::exists(receipt) && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    expect(ca::fs::is_regular_file(receipt), "Shell launched the app in the expected mode");
    const auto options = ca::readJob(receipt);
    expect(options.action == action && options.files == files, "Shell handoff preserved action and selection order");
}
}
int wmain(int argc,wchar_t** argv){
    if (ca::fs::path(argv[0]).filename() == L"ContextActions.exe") return receiveLaunch(argc, argv);
    const auto apartment=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);int result=1;
    try{
        ca::check(apartment);expect(argc==3,"Pass shell DLL and isolated test directory");
        auto root=ca::fs::absolute(argv[2])/std::to_wstring(GetCurrentProcessId());ca::fs::create_directories(root);
        SetEnvironmentVariableW(L"CONTEXT_ACTIONS_DATA_DIR",(root/L"state").c_str());ca::setLanguage(L"en");ca::resetOfficeCapabilities();
        const auto isolatedDll = root / ca::fs::path(argv[1]).filename();
        ca::fs::copy_file(argv[1], isolatedDll, ca::fs::copy_options::overwrite_existing);
        ca::fs::copy_file(argv[0], root / L"ContextActions.exe", ca::fs::copy_options::overwrite_existing);
        struct Module {HMODULE value;~Module(){if(value)FreeLibrary(value);}} module{LoadLibraryW(isolatedDll.c_str())};expect(module.value!=nullptr,"Load shell DLL");
        auto factoryFn=reinterpret_cast<HRESULT(WINAPI*)(REFCLSID,REFIID,void**)>(GetProcAddress(module.value,"DllGetClassObject"));
        auto unloadFn=reinterpret_cast<HRESULT(WINAPI*)()>(GetProcAddress(module.value,"DllCanUnloadNow"));expect(factoryFn&&unloadFn,"COM exports");
        expect(unloadFn()==S_OK,"Initially unloadable");
        {
            ca::ComPtr<IClassFactory> factory;ca::check(factoryFn(commandClsid,IID_PPV_ARGS(&factory)));
            ca::ComPtr<IExplorerCommand> menu;ca::check(factory->CreateInstance(nullptr,IID_PPV_ARGS(&menu)));
            expect(unloadFn()==S_FALSE,"Live COM objects prevent unloading");
            EXPCMDFLAGS flags{};ca::check(menu->GetFlags(&flags));expect(flags==ECF_HASSUBCOMMANDS,"Root submenu flag");
            PWSTR title{};ca::check(menu->GetTitle(nullptr,&title));std::wstring rootTitle=title;CoTaskMemFree(title);expect(rootTitle==L"Context Actions","Root title");
            ca::ComPtr<IEnumExplorerCommand> enumeration;ca::check(menu->EnumSubCommands(&enumeration));
            expect(enumeration->Next(1,nullptr,nullptr)==E_POINTER,"Enumerator validates output pointer");
            std::map<ca::Action,ca::ComPtr<IExplorerCommand>> commands;
            for(;;){ca::ComPtr<IExplorerCommand> command;auto hr=enumeration->Next(1,&command,nullptr);if(hr==S_FALSE)break;ca::check(hr);
                GUID id{};ca::check(command->GetCanonicalName(&id));auto action=static_cast<ca::Action>(id.Data1-commandClsid.Data1-1);
                expect(commands.emplace(action,command).second,"Unique command identifiers");ca::check(command->GetFlags(&flags));expect(flags==ECF_DEFAULT,"Leaf command flag");
                ca::check(command->GetTitle(nullptr,&title));std::wstring text=title;CoTaskMemFree(title);expect(!text.empty(),"Localized command title");
            }
            expect(commands.size()==15&&!commands.contains(ca::Action::documents)&&!commands.contains(ca::Action::sheets),"Supported commands only, including More");
            ca::check(enumeration->Reset());ca::check(enumeration->Skip(1));ca::ComPtr<IEnumExplorerCommand> clone;ca::check(enumeration->Clone(&clone));
            ca::ComPtr<IExplorerCommand> originalNext,cloneNext;ca::check(enumeration->Next(1,&originalNext,nullptr));ca::check(clone->Next(1,&cloneNext,nullptr));
            GUID first{},second{};ca::check(originalNext->GetCanonicalName(&first));ca::check(cloneNext->GetCanonicalName(&second));expect(first==second,"Clone preserves position");expect(enumeration->Skip(100)==S_FALSE,"Skip reports end");
            EXPCMDSTATE state{};ca::check(menu->GetState(nullptr,FALSE,&state));expect(state==ECS_HIDDEN,"Empty root hidden");expect(menu->Invoke(nullptr,nullptr)==E_INVALIDARG,"Empty invocation rejected");
            for(auto const& [action,command]:commands){ca::check(command->GetState(nullptr,FALSE,&state));expect(state==ECS_HIDDEN,"Empty child hidden");}
            using A=ca::Action;
            auto checkSelection=[&](std::initializer_list<const wchar_t*> files,std::set<A> enabled){
                auto items=selection(root,files);ca::check(menu->GetState(items.Get(),FALSE,&state));expect(state==ECS_ENABLED,"Selected root enabled");
                for(auto const& [action,command]:commands)for(auto slow:{FALSE,TRUE}){ca::check(command->GetState(items.Get(),slow,&state));const auto expected=static_cast<EXPCMDSTATE>(enabled.contains(action)?ECS_ENABLED:ECS_HIDDEN);if(state!=expected)throw std::runtime_error("Visibility: "+ca::utf8(*files.begin())+" / "+ca::utf8(ca::actionId(action))+" actual="+std::to_string(state)+" expected="+std::to_string(expected));}
            };
            checkSelection({L"кино & 100% !.mp4",L"second.mov"},{A::more,A::compress,A::h264,A::h265,A::telegram,A::audio,A::remux,A::frame,A::mediaInfo,A::rename});
            checkSelection({L"picture.png"},{A::more,A::image,A::pdf,A::mediaInfo,A::rename});
            checkSelection({L"picture.png",L"pages.pdf"},{A::more,A::pdf,A::rename});
            checkSelection({L"pages.pdf"},{A::more,A::pdf,A::rename});
            checkSelection({L"document.docx"},{A::more,A::word,A::rename});
            checkSelection({L"table.xlsx"},{A::more,A::excel,A::rename});
            checkSelection({L"notes.txt"},{A::more,A::word,A::rename});
            checkSelection({L"unknown.bin"},{A::more,A::rename});
            checkSelection({L"picture.png",L"кино & 100% !.mp4"},{A::more,A::mediaInfo,A::rename});
            checkSelection({L"track.wav",L"song.opus"},{A::more,A::audio,A::mediaInfo,A::rename});
            checkSelection({L"track.wav",L"second.mov"},{A::more,A::audio,A::mediaInfo,A::rename});
            checkSelection({L"track.wav",L"picture.png"},{A::more,A::mediaInfo,A::rename});
            checkSelection({L"архив.ZIP",L"backup.tgz",L"other.rar"},{A::more,A::repack,A::rename});
            checkSelection({L"backup.tar",L"other.7z"},{A::more,A::repack,A::rename});
            checkSelection({L"backup.tar",L"notes.txt"},{A::more,A::rename});
            auto allTargets=selection(root,{L"архив.ZIP",L"backup.tgz",L"other.7z"});
            ca::check(commands.at(A::repack)->GetState(allTargets.Get(),FALSE,&state));
            expect(state==ECS_DISABLED,"Repack disabled without a common target");
            expect(commands.at(A::repack)->Invoke(allTargets.Get(),nullptr)==E_INVALIDARG,"Invocation without a common target rejected");
            auto mixed=selection(root,{L"backup.tar",L"notes.txt"});
            expect(commands.at(A::repack)->Invoke(mixed.Get(),nullptr)==E_INVALIDARG,"Invalid archive invocation rejected");
            auto image=selection(root,{L"picture.png"});expect(commands.at(A::h264)->Invoke(image.Get(),nullptr)==E_INVALIDARG,"Unsupported invocation rejected");
            auto files = selection(root, {L"кино & 100% !.mp4", L"second.mov"});
            const std::vector<ca::fs::path> selected{root / L"кино & 100% !.mp4", root / L"second.mov"};
            expectLaunch([&] { return commands.at(A::more)->Invoke(files.Get(), nullptr); }, root / L"state/chooser.caj", A::more, selected);
            expectLaunch([&] { return commands.at(A::h264)->Invoke(files.Get(), nullptr); }, root / L"state/direct.caj", A::h264, selected);
            expect(ca::fs::is_empty(root / L"state/jobs"), "GUI launches consume their private shell jobs");
            ca::setOfficeCapability(false,ca::Capability::unavailable);auto doc=selection(root,{L"document.docx"});ca::check(commands.at(A::word)->GetState(doc.Get(),FALSE,&state));expect(state==ECS_DISABLED,"Unavailable Office disabled");
        }
        {
            ca::ComPtr<IClassFactory> factory;
            ca::check(factoryFn(classicClsid, IID_PPV_ARGS(&factory)));
            ca::ComPtr<IExecuteCommand> command;
            ca::check(factory->CreateInstance(nullptr, IID_PPV_ARGS(&command)));
            expect(command->Execute() == E_INVALIDARG, "Windows 10 verb requires a selection");
            ca::ComPtr<IObjectWithSelection> withSelection;
            ca::check(command.As(&withSelection));
            auto files = selection(root, {L"кино & 100% !.mp4", L"picture.png"});
            ca::check(withSelection->SetSelection(files.Get()));
            ca::ComPtr<IShellItemArray> actual;
            ca::check(withSelection->GetSelection(IID_PPV_ARGS(&actual)));
            expect(actual.Get() == files.Get(), "DelegateExecute retains the selection");
            ca::check(command->SetNoShowUI(TRUE));
            expect(command->Execute() == HRESULT_FROM_WIN32(ERROR_CANCELLED), "Windows 10 verb respects UI suppression");
            ca::check(command->SetNoShowUI(FALSE));
            expectLaunch([&] { return command->Execute(); }, root / L"state/chooser.caj", ca::Action::more,
                         {root / L"кино & 100% !.mp4", root / L"picture.png"});
            expect(ca::fs::is_empty(root / L"state/jobs"), "Windows 10 chooser consumes its private shell job");
        }
        expect(unloadFn()==S_OK,"All COM references released");
        std::cout<<"PASS: actual shell DLL, COM lifetime/enumeration, selection filtering, Office availability, and modern/classic launch handoffs.\n";result=0;
    }catch(std::exception const& e){std::cerr<<e.what()<<'\n';}
    if(SUCCEEDED(apartment))CoUninitialize();return result;
}
