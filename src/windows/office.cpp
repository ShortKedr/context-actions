#include "windows/platform_win.h"
#include <algorithm>
#include <comdef.h>

namespace ca {
namespace {
const wchar_t* officeKey(bool excel) {return excel?L"Excel":L"Word";}
struct Variant : VARIANT {
    Variant() {VariantInit(this);}
    ~Variant(){VariantClear(this);}
    Variant(const Variant& other){VariantInit(this);check(VariantCopy(this,const_cast<Variant*>(&other)));}
    Variant(Variant&& other) noexcept {static_cast<VARIANT&>(*this)=other;VariantInit(&other);}
    explicit Variant(const std::wstring& text):Variant(){vt=VT_BSTR;bstrVal=SysAllocString(text.c_str());if(!bstrVal)throw std::bad_alloc();}
    explicit Variant(long value):Variant(){vt=VT_I4;lVal=value;}
    explicit Variant(bool value):Variant(){vt=VT_BOOL;boolVal=value?VARIANT_TRUE:VARIANT_FALSE;}
    static Variant missing(){Variant v;v.vt=VT_ERROR;v.scode=DISP_E_PARAMNOTFOUND;return v;}
};
Variant invoke(IDispatch* object,const wchar_t* name,WORD flags,std::vector<Variant> arguments={}) {
    DISPID id;auto member=const_cast<wchar_t*>(name);
    check(object->GetIDsOfNames(IID_NULL,&member,1,LOCALE_USER_DEFAULT,&id));
    // VARIANT has no extra data in Variant; marshal an explicit contiguous VARIANT array.
    std::vector<VARIANT> raw;for(auto it=arguments.rbegin();it!=arguments.rend();++it)raw.push_back(*it);
    DISPID put=DISPID_PROPERTYPUT;
    DISPPARAMS params{raw.data(),flags&DISPATCH_PROPERTYPUT?&put:nullptr,static_cast<UINT>(raw.size()),flags&DISPATCH_PROPERTYPUT?1u:0u};
    Variant result;EXCEPINFO error{};
    HRESULT hr=object->Invoke(id,IID_NULL,LOCALE_USER_DEFAULT,flags,&params,&result,&error,nullptr);
    std::wstring message=error.bstrDescription?error.bstrDescription:L"";
    SysFreeString(error.bstrSource);SysFreeString(error.bstrDescription);SysFreeString(error.bstrHelpFile);
    if(FAILED(hr))throw std::runtime_error(utf8(message.empty()?systemError(static_cast<DWORD>(hr)):message));
    return result;
}
ComPtr<IDispatch> dispatch(Variant value) {
    if(value.vt!=VT_DISPATCH||!value.pdispVal)throw std::runtime_error("Office returned no object");
    ComPtr<IDispatch> result=value.pdispVal;return result;
}
void put(IDispatch* object,const wchar_t* name,Variant value){invoke(object,name,DISPATCH_PROPERTYPUT,{value});}
struct OfficeSession {
    ComPtr<IDispatch> app,document,wordOptions;
    bool oldUpdateLinks=false,restoreLinks=false;
    ~OfficeSession(){
        try{if(document)invoke(document.Get(),L"Close",DISPATCH_METHOD,{Variant(false)});}catch(...){}
        try{if(restoreLinks)put(wordOptions.Get(),L"UpdateLinksAtOpen",Variant(oldUpdateLinks));}catch(...){}
        try{if(app)invoke(app.Get(),L"Quit",DISPATCH_METHOD);}catch(...){}
    }
};
}
Capability officeCapability(bool excel) {
    int value=GetPrivateProfileIntW(L"office",officeKey(excel),0,(dataDirectory()/L"capabilities.ini").c_str());
    return value==1?Capability::available:value==2?Capability::unavailable:Capability::unknown;
}
std::wstring officeUnavailableReason(bool excel) { return tr(excel ? "needExcel" : "needWord"); }
const char* officeActionKey(bool excel) { return excel ? "excel" : "word"; }
void setOfficeCapability(bool excel,Capability state) {
    fs::create_directories(dataDirectory());
    if(!WritePrivateProfileStringW(L"office",officeKey(excel),std::to_wstring(static_cast<int>(state)).c_str(),(dataDirectory()/L"capabilities.ini").c_str()))
        throw std::runtime_error(utf8(systemError(GetLastError())));
}
void resetOfficeCapabilities(){setOfficeCapability(false,Capability::unknown);setOfficeCapability(true,Capability::unknown);}
void exportOfficePdf(const fs::path& input,const fs::path& output,bool excel,std::atomic_bool& cancel) {
    if(cancel)throw Cancelled();
    if(officeCapability(excel)==Capability::unavailable)throw std::runtime_error(utf8(tr(excel?"needExcel":"needWord")));
    CLSID clsid{};OfficeSession session;
    HRESULT hr=CLSIDFromProgID(excel?L"Excel.Application":L"Word.Application",&clsid);
    if(SUCCEEDED(hr))hr=CoCreateInstance(clsid,nullptr,CLSCTX_LOCAL_SERVER,IID_PPV_ARGS(&session.app));
    if(FAILED(hr)) {
        setOfficeCapability(excel,Capability::unavailable);
        throw std::runtime_error(utf8(tr(excel?"needExcel":"needWord")+L"\n"+systemError(static_cast<DWORD>(hr))));
    }
    setOfficeCapability(excel,Capability::available);
    put(session.app.Get(),L"Visible",Variant(false));
    put(session.app.Get(),L"AutomationSecurity",Variant(3L)); // msoAutomationSecurityForceDisable
    put(session.app.Get(),L"DisplayAlerts",Variant(0L));
    if(cancel)throw Cancelled();
    if(excel) {
        put(session.app.Get(),L"AskToUpdateLinks",Variant(false));
        auto books=dispatch(invoke(session.app.Get(),L"Workbooks",DISPATCH_PROPERTYGET));
        session.document=dispatch(invoke(books.Get(),L"Open",DISPATCH_METHOD,{
            Variant(input.wstring()),Variant(0L),Variant(true),Variant::missing(),Variant(std::wstring(L"ContextActions-no-password")),
            Variant::missing(),Variant(true),Variant::missing(),Variant::missing(),Variant(false),Variant(false),Variant::missing(),Variant(false)}));
        if(cancel)throw Cancelled();
        invoke(session.document.Get(),L"ExportAsFixedFormat",DISPATCH_METHOD,{Variant(0L),Variant(output.wstring()),Variant(0L),Variant(true),Variant(false)});
    } else {
        session.wordOptions=dispatch(invoke(session.app.Get(),L"Options",DISPATCH_PROPERTYGET));
        auto previous=invoke(session.wordOptions.Get(),L"UpdateLinksAtOpen",DISPATCH_PROPERTYGET);
        session.oldUpdateLinks=previous.vt==VT_BOOL&&previous.boolVal==VARIANT_TRUE;
        session.restoreLinks=true;put(session.wordOptions.Get(),L"UpdateLinksAtOpen",Variant(false));
        auto docs=dispatch(invoke(session.app.Get(),L"Documents",DISPATCH_PROPERTYGET));
        session.document=dispatch(invoke(docs.Get(),L"Open",DISPATCH_METHOD,{
            Variant(input.wstring()),Variant(false),Variant(true),Variant(false),Variant(std::wstring(L"ContextActions-no-password")),
            Variant::missing(),Variant(false),Variant::missing(),Variant::missing(),Variant::missing(),Variant::missing(),
            Variant(false),Variant(false),Variant(false),Variant::missing(),Variant(true)}));
        if(cancel)throw Cancelled();
        invoke(session.document.Get(),L"ExportAsFixedFormat",DISPATCH_METHOD,{Variant(output.wstring()),Variant(17L),Variant(false)});
    }
    if(cancel)throw Cancelled();
}
}
