#include "platform_win.h"
#include <shobjidl.h>
#include <shlwapi.h>
#include <wrl/implements.h>

using namespace Microsoft::WRL;
namespace {
// Keep synchronized with packaging/windows/AppxManifest.xml and register-shell.ps1.
const CLSID commandClsid={0xa836fb4f,0x6548,0x4fc3,{0x8d,0x67,0x50,0x26,0x7b,0x25,0xa4,0x18}};
// Windows 10's DelegateExecute verb is registered by register-shell.ps1.
const CLSID classicClsid={0xee9d6c0b,0x0a19,0x4f8c,{0x8e,0x61,0xc7,0x8d,0xd8,0x81,0x6d,0xb0}};
HMODULE module=nullptr;std::atomic_long objects=0;
ca::fs::path directory(){wchar_t path[32768];GetModuleFileNameW(module,path,32768);return ca::fs::path(path).parent_path();}
std::vector<ca::fs::path> selection(IShellItemArray* items){
    std::vector<ca::fs::path> files;if(!items)return files;DWORD count=0;ca::check(items->GetCount(&count));
    if(count>100000)throw std::runtime_error("Too many files");
    for(DWORD i=0;i<count;++i){ComPtr<IShellItem> item;ca::check(items->GetItemAt(i,&item));PWSTR path=nullptr;ca::check(item->GetDisplayName(SIGDN_FILESYSPATH,&path));files.emplace_back(path);CoTaskMemFree(path);}
    return files;
}
HRESULT launchAction(ca::Action action, IShellItemArray* items) noexcept {
    try {
        ca::Options options;
        options.action = action;
        options.files = selection(items);
        if (options.files.empty() || !ca::accepts(action, options.files) ||
            (action == ca::Action::repack && !ca::unavailableReason(action, options.files).empty()))
            return E_INVALIDARG;
        const auto job = ca::writeJob(options);
        try {
            std::vector<std::wstring> args{L"--job", job.wstring()};
            if (action == ca::Action::more) args.push_back(L"--choose-action");
            ca::launch(directory() / L"ContextActions.exe", args);
        } catch (...) {
            ca::consumeJob(job);
            throw;
        }
        return S_OK;
    } catch (...) {
        return E_FAIL;
    }
}
class Command;
class Enumerator final:public RuntimeClass<RuntimeClassFlags<ClassicCom>,IEnumExplorerCommand>{
    std::vector<ComPtr<IExplorerCommand>> commands;ULONG position=0;
public:
    explicit Enumerator(std::vector<ComPtr<IExplorerCommand>> values,ULONG index=0):commands(std::move(values)),position(index){++objects;}
    ~Enumerator(){--objects;}
    IFACEMETHODIMP Next(ULONG count,IExplorerCommand** values,ULONG* fetched) override{
        if(!values||(!fetched&&count!=1))return E_POINTER;ULONG n=0;
        while(n<count&&position<commands.size()){values[n]=commands[position++].Get();values[n++]->AddRef();}
        if(fetched)*fetched=n;return n==count?S_OK:S_FALSE;
    }
    IFACEMETHODIMP Skip(ULONG count) override{auto remaining=static_cast<ULONG>(commands.size())-position;position+=std::min(count,remaining);return count<=remaining?S_OK:S_FALSE;}
    IFACEMETHODIMP Reset() override{position=0;return S_OK;}
    IFACEMETHODIMP Clone(IEnumExplorerCommand** result) override{if(!result)return E_POINTER;auto copy=Make<Enumerator>(commands,position);return copy?copy.CopyTo(result):E_OUTOFMEMORY;}
};
class Command final:public RuntimeClass<RuntimeClassFlags<ClassicCom>,IExplorerCommand>{
    bool root;ca::Action action;
public:
    explicit Command(bool isRoot=true,ca::Action selected=ca::Action::more):root(isRoot),action(selected){++objects;}
    ~Command(){--objects;}
    IFACEMETHODIMP GetTitle(IShellItemArray*,PWSTR* title) override{
        if(!title)return E_POINTER;*title=nullptr;try{return SHStrDupW(root?L"Context Actions":ca::tr(ca::actionKey(action)).c_str(),title);}catch(...){return E_FAIL;}
    }
    IFACEMETHODIMP GetIcon(IShellItemArray*,PWSTR* icon) override{
        if(!icon)return E_POINTER;*icon=nullptr;try{return SHStrDupW(((directory()/L"ContextActionsShell.dll").wstring()+L",-101").c_str(),icon);}catch(...){return E_FAIL;}
    }
    IFACEMETHODIMP GetToolTip(IShellItemArray*,PWSTR* tooltip) override{if(!tooltip)return E_POINTER;*tooltip=nullptr;return E_NOTIMPL;}
    IFACEMETHODIMP GetCanonicalName(GUID* id) override{if(!id)return E_POINTER;*id=commandClsid;if(!root)id->Data1+=static_cast<ULONG>(action)+1;return S_OK;}
    IFACEMETHODIMP GetState(IShellItemArray* items,BOOL,EXPCMDSTATE* state) override{
        if(!state)return E_POINTER;*state=ECS_HIDDEN;
        try{
            auto files=selection(items);if(files.empty())return S_OK;
            if(root||action==ca::Action::more){*state=ECS_ENABLED;return S_OK;}
            if(ca::accepts(action,files))*state=ca::unavailableReason(action,files).empty()?ECS_ENABLED:ECS_DISABLED;
            return S_OK;
        }catch(...){return S_OK;}
    }
    IFACEMETHODIMP Invoke(IShellItemArray* items,IBindCtx*) override{
        return launchAction(action, items);
    }
    IFACEMETHODIMP GetFlags(EXPCMDFLAGS* flags) override{if(!flags)return E_POINTER;*flags=root?ECF_HASSUBCOMMANDS:ECF_DEFAULT;return S_OK;}
    IFACEMETHODIMP EnumSubCommands(IEnumExplorerCommand** result) override{
        if(!result)return E_POINTER;*result=nullptr;if(!root)return E_NOTIMPL;
        try{
            std::vector<ComPtr<IExplorerCommand>> commands;
            for(auto a:ca::catalog()){if(a==ca::Action::documents||a==ca::Action::sheets)continue;auto command=Make<Command>(false,a);if(!command)return E_OUTOFMEMORY;commands.push_back(command);}
            commands.push_back(Make<Command>(false,ca::Action::more));
            auto enumerator=Make<Enumerator>(std::move(commands));return enumerator?enumerator.CopyTo(result):E_OUTOFMEMORY;
        }catch(...){return E_OUTOFMEMORY;}
    }
};
class ClassicCommand final : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IExecuteCommand, IObjectWithSelection> {
    ComPtr<IShellItemArray> items;
    bool noShowUI = false;

public:
    ClassicCommand() { ++objects; }
    ~ClassicCommand() { --objects; }

    IFACEMETHODIMP SetKeyState(DWORD) override { return S_OK; }
    IFACEMETHODIMP SetParameters(PCWSTR) override { return S_OK; }
    IFACEMETHODIMP SetPosition(POINT) override { return S_OK; }
    IFACEMETHODIMP SetShowWindow(int) override { return S_OK; }
    IFACEMETHODIMP SetDirectory(PCWSTR) override { return S_OK; }
    IFACEMETHODIMP SetNoShowUI(BOOL suppress) override { noShowUI = suppress != FALSE; return S_OK; }
    IFACEMETHODIMP Execute() override {
        if (noShowUI) return HRESULT_FROM_WIN32(ERROR_CANCELLED);
        return launchAction(ca::Action::more, items.Get());
    }
    IFACEMETHODIMP SetSelection(IShellItemArray* selection) override { items = selection; return S_OK; }
    IFACEMETHODIMP GetSelection(REFIID iid, void** result) override {
        if (!result) return E_POINTER;
        *result = nullptr;
        return items ? items.CopyTo(iid, result) : E_FAIL;
    }
};
class Factory final:public RuntimeClass<RuntimeClassFlags<ClassicCom>,IClassFactory>{
    bool classic;
public:
    explicit Factory(bool useClassic):classic(useClassic){++objects;}~Factory(){--objects;}
    IFACEMETHODIMP CreateInstance(IUnknown* outer,REFIID id,void** result) override{
        if(!result)return E_POINTER;
        *result=nullptr;
        if(outer)return CLASS_E_NOAGGREGATION;
        if(classic){auto command=Make<ClassicCommand>();return command?command->QueryInterface(id,result):E_OUTOFMEMORY;}
        auto command=Make<Command>();return command?command->QueryInterface(id,result):E_OUTOFMEMORY;
    }
    IFACEMETHODIMP LockServer(BOOL lock) override{if(lock)++objects;else --objects;return S_OK;}
};
}
extern "C" HRESULT __stdcall DllGetClassObject(REFCLSID clsid,REFIID iid,void** result){if(!result)return E_POINTER;*result=nullptr;if(clsid!=commandClsid&&clsid!=classicClsid)return CLASS_E_CLASSNOTAVAILABLE;auto factory=Make<Factory>(clsid==classicClsid);return factory?factory->QueryInterface(iid,result):E_OUTOFMEMORY;}
extern "C" HRESULT __stdcall DllCanUnloadNow(){return objects==0?S_OK:S_FALSE;}
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,void*){if(reason==DLL_PROCESS_ATTACH){module=instance;DisableThreadLibraryCalls(instance);}return TRUE;}
