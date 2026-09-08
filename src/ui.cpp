#include "core.h"
#include <commctrl.h>
#include <dwmapi.h>
#include <shlobj.h>
#include <shellapi.h>
#include <uxtheme.h>
#include <algorithm>
#include <map>
#include <thread>

namespace ca {
namespace {
enum Id { add=100,remove,up,down,files,action,output,browse,quality,width,fps,target,mute,format,firstFrame,pattern,prefix,suffix,find,replace,start,preview,run,cancelButton,open,reveal,retryOffice,undo,lang,theme,progress,status,hint };
constexpr UINT updated=WM_APP+1,finished=WM_APP+2,autorun=WM_APP+3;
const auto actions=catalog();
struct Field {int id;const char* key;const wchar_t* initial;};
const Field numericFields[]{{quality,"quality",L"24"},{width,"width",L"0"},{fps,"fps",L"0"},{target,"target",L"0"}};
const Field renameFields[]{{pattern,"pattern",L"{name}_{n}"},{prefix,"prefix",L""},{suffix,"suffix",L""},{find,"find",L""},{replace,"replace",L""},{start,"start",L"1"}};
struct Window {
    HWND hwnd=nullptr;std::map<int,HWND> controls,labels;
    Options options;std::vector<fs::path> outputs;std::vector<double> fractions;
    std::thread worker;std::atomic_bool cancelled=false;
    bool busy=false,previewed=false,closeAfter=false,dark=false;int scroll=0,dpi=96;
    bool smoke=false;std::string error;
    HFONT font=nullptr;HBRUSH background=nullptr;COLORREF bg=0,fg=0;
    ~Window(){if(worker.joinable())worker.join();if(font)DeleteObject(font);if(background)DeleteObject(background);}
    int px(int n)const{return MulDiv(n,dpi,96);}
    std::wstring text(int id){int length=GetWindowTextLengthW(controls.at(id));std::wstring value(length+1,L'\0');GetWindowTextW(controls.at(id),value.data(),length+1);value.resize(length);return value;}
    void text(int id,const std::wstring& value){SetWindowTextW(controls.at(id),value.c_str());}
    int number(int id){auto value=text(id);size_t used=0;int n=std::stoi(value,&used);if(used!=value.size())throw std::runtime_error(utf8(tr("invalid")));return n;}
    HWND make(int id,const wchar_t* type,const std::wstring& title,DWORD style=0){
        HWND control=CreateWindowExW(wcscmp(type,L"EDIT")==0?WS_EX_CLIENTEDGE:0,type,title.c_str(),WS_CHILD|WS_VISIBLE|WS_TABSTOP|style,0,0,10,10,hwnd,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
        if(!control)check(HRESULT_FROM_WIN32(GetLastError()));controls[id]=control;return control;
    }
    void label(int id,const char* key){auto h=CreateWindowExW(0,L"STATIC",tr(key).c_str(),WS_CHILD|WS_VISIBLE,0,0,10,10,hwnd,nullptr,GetModuleHandleW(nullptr),nullptr);labels[id]=h;}
    void field(int id,const char* key,const wchar_t* value,DWORD style=ES_AUTOHSCROLL){label(id,key);make(id,L"EDIT",value,style);}
    void button(int id,const char* key){make(id,L"BUTTON",tr(key),BS_PUSHBUTTON);}
    void init(){
        dpi=static_cast<int>(GetDpiForWindow(hwnd));
        button(add,"add");button(remove,"remove");button(up,"up");button(down,"down");
        auto list=make(files,WC_LISTVIEWW,L"",LVS_REPORT|LVS_SHOWSELALWAYS);ListView_SetExtendedListViewStyle(list,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER|LVS_EX_LABELTIP);
        LVCOLUMNW column{};column.mask=LVCF_TEXT|LVCF_WIDTH;column.cx=420;auto title=tr("file");column.pszText=title.data();ListView_InsertColumn(list,0,&column);title=tr("status");column.pszText=title.data();column.cx=360;ListView_InsertColumn(list,1,&column);
        label(action,"action");make(action,L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL);
        label(lang,"language");make(lang,L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL);
        for(const auto* name:{L"English",L"Русский",L"Deutsch",L"Français",L"简体中文"})SendMessageW(controls[lang],CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name));
        auto l=language();SendMessageW(controls[lang],CB_SETCURSEL,l.starts_with(L"ru")?1:l.starts_with(L"de")?2:l.starts_with(L"fr")?3:l.starts_with(L"zh")?4:0,0);
        label(theme,"theme");make(theme,L"COMBOBOX",L"",CBS_DROPDOWNLIST);
        for(auto key:{"system","light","dark"}){auto t=tr(key);SendMessageW(controls[theme],CB_ADDSTRING,0,reinterpret_cast<LPARAM>(t.c_str()));}
        int savedTheme=GetPrivateProfileIntW(L"ui",L"theme",0,(dataDirectory()/L"settings.ini").c_str());SendMessageW(controls[theme],CB_SETCURSEL,std::clamp(savedTheme,0,2),0);
        field(output,"output",options.output.c_str());button(browse,"browse");
        for(const auto& f:numericFields)field(f.id,f.key,f.initial,ES_NUMBER|ES_AUTOHSCROLL);
        text(quality,std::to_wstring(options.quality));text(width,std::to_wstring(options.width));text(fps,std::to_wstring(options.fps));text(target,std::to_wstring(options.targetMB));
        make(mute,L"BUTTON",tr("mute"),BS_AUTOCHECKBOX);SendMessageW(controls[mute],BM_SETCHECK,options.mute?BST_CHECKED:BST_UNCHECKED,0);
        make(firstFrame,L"BUTTON",tr("firstFrame"),BS_AUTOCHECKBOX);SendMessageW(controls[firstFrame],BM_SETCHECK,options.firstFrame?BST_CHECKED:BST_UNCHECKED,0);
        label(format,"format");make(format,L"COMBOBOX",L"",CBS_DROPDOWNLIST);
        for(auto f:{L"png",L"jpg",L"webp",L"bmp",L"tiff",L"gif"})SendMessageW(controls[format],CB_ADDSTRING,0,reinterpret_cast<LPARAM>(f));
        SendMessageW(controls[format],CB_SELECTSTRING,static_cast<WPARAM>(-1),reinterpret_cast<LPARAM>(options.format.c_str()));
        for(const auto& f:renameFields)field(f.id,f.key,f.initial);
        text(pattern,options.pattern);text(prefix,options.prefix);text(suffix,options.suffix);text(find,options.find);text(replace,options.replace);text(start,std::to_wstring(options.start));
        button(preview,"preview");button(run,"run");button(cancelButton,"cancel");button(open,"open");button(reveal,"reveal");button(retryOffice,"retryOffice");button(undo,"undo");
        make(progress,PROGRESS_CLASSW,L"",PBS_SMOOTH);SendMessageW(controls[progress],PBM_SETRANGE32,0,1000);
        make(status,L"STATIC",tr("ready"),SS_LEFT);make(hint,L"STATIC",L"",SS_LEFT);
        EnableWindow(controls[cancelButton],FALSE);EnableWindow(controls[open],FALSE);EnableWindow(controls[reveal],FALSE);
        refreshFiles();refreshActions();applyTheme();layout();DragAcceptFiles(hwnd,TRUE);
    }
    void refreshFiles(){
        ListView_DeleteAllItems(controls[files]);outputs.resize(options.files.size());fractions.assign(options.files.size(),0);
        for(size_t i=0;i<options.files.size();++i){LVITEMW item{};item.mask=LVIF_TEXT;item.iItem=static_cast<int>(i);auto path=options.files[i].wstring();item.pszText=path.data();ListView_InsertItem(controls[files],&item);}
        previewed=false;
    }
    Action selected(){int i=static_cast<int>(SendMessageW(controls[action],CB_GETCURSEL,0,0));return i>=0?actions.at(i):Action::compress;}
    void refreshActions(){
        auto chosen=options.action;
        if(chosen==Action::more){auto it=std::find_if(actions.begin(),actions.end(),[&](auto a){return unavailableReason(a,options.files).empty();});chosen=it==actions.end()?Action::compress:*it;}
        SendMessageW(controls[action],CB_RESETCONTENT,0,0);
        for(size_t i=0;i<actions.size();++i){auto a=actions[i];auto title=tr(actionKey(a));auto reason=unavailableReason(a,options.files);if(!reason.empty())title+=L" — "+reason;SendMessageW(controls[action],CB_ADDSTRING,0,reinterpret_cast<LPARAM>(title.c_str()));if(a==chosen)SendMessageW(controls[action],CB_SETCURSEL,i,0);}
        options.action=selected();updateAction();
    }
    void updateAction(){
        options.action=selected();auto a=options.action;previewed=false;
        SetWindowTextW(labels[quality],tr(a==Action::image?"imageQuality":"quality").c_str());
        auto reason=unavailableReason(a,options.files);
        const char* key=a==Action::image?"imageHint":a==Action::pdf?"pdfHint":a==Action::rename?"renameHint":(a==Action::word||a==Action::excel)?"officeHint":"videoHint";
        text(hint,reason.empty()?tr(key):reason);
        EnableWindow(controls[run],!busy&&reason.empty());layout();
    }
    void applyTheme(){
        HIGHCONTRASTW contrast{sizeof(contrast)};SystemParametersInfoW(SPI_GETHIGHCONTRAST,sizeof(contrast),&contrast,0);
        auto choice=SendMessageW(controls[theme],CB_GETCURSEL,0,0);DWORD light=1,size=sizeof(light);
        RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",L"AppsUseLightTheme",RRF_RT_REG_DWORD,nullptr,&light,&size);
        bool high=(contrast.dwFlags&HCF_HIGHCONTRASTON)!=0;dark=!high&&(choice==2||(choice==0&&!light));
        bg=high?GetSysColor(COLOR_WINDOW):dark?RGB(30,30,30):RGB(248,248,248);fg=high?GetSysColor(COLOR_WINDOWTEXT):dark?RGB(240,240,240):RGB(24,24,24);
        if(background)DeleteObject(background);background=CreateSolidBrush(bg);BOOL value=dark;DwmSetWindowAttribute(hwnd,DWMWA_USE_IMMERSIVE_DARK_MODE,&value,sizeof(value));
        ListView_SetBkColor(controls[files],bg);ListView_SetTextBkColor(controls[files],bg);ListView_SetTextColor(controls[files],fg);
        if(font)DeleteObject(font);NONCLIENTMETRICSW metrics{sizeof(metrics)};SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS,sizeof(metrics),&metrics,0,dpi);font=CreateFontIndirectW(&metrics.lfMessageFont);
        for(const auto& [id,h]:controls){SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);if(id==files)SetWindowTheme(h,L"Explorer",nullptr);}
        for(const auto& [id,h]:labels)SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
        InvalidateRect(hwnd,nullptr,TRUE);
    }
    void layout(){
        if(!controls.contains(hint))return;
        RECT rect;GetClientRect(hwnd,&rect);int w=MulDiv(rect.right,96,dpi),h=MulDiv(rect.bottom,96,dpi),gap=12,margin=20,col=(w-2*margin-2*gap)/3;
        const int full=740;scroll=std::clamp(scroll,0,std::max(0,full-h));
        SCROLLINFO info{sizeof(info),SIF_RANGE|SIF_PAGE|SIF_POS,0,full-1,static_cast<UINT>(h),scroll,0};SetScrollInfo(hwnd,SB_VERT,&info,TRUE);
        auto move=[&](HWND c,int x,int y,int width_,int height_){MoveWindow(c,px(x),px(y-scroll),px(std::max(10,width_)),px(height_),TRUE);};
        auto place=[&](int id,int x,int y,int width_,int height_=30){move(controls[id],x,y,width_,height_);};
        auto fieldAt=[&](int id,int x,int y,int width_){move(labels[id],x,y,width_,22);place(id,x,y+24,width_,id==action||id==format||id==lang||id==theme?220:28);};
        place(add,margin,18,140);place(remove,margin+152,18,110);place(up,margin+274,18,110);place(down,margin+396,18,110);
        place(files,margin,60,w-2*margin,180);ListView_SetColumnWidth(controls[files],0,px((w-2*margin)*55/100));ListView_SetColumnWidth(controls[files],1,px((w-2*margin)*43/100));
        fieldAt(action,margin,254,w-2*margin);
        fieldAt(output,margin,318,w-2*margin-125);place(browse,w-margin-113,342,113);
        for(const auto& f:numericFields){ShowWindow(controls[f.id],SW_HIDE);ShowWindow(labels[f.id],SW_HIDE);}
        for(const auto& f:renameFields){ShowWindow(controls[f.id],SW_HIDE);ShowWindow(labels[f.id],SW_HIDE);}
        for(int id:{mute,format,firstFrame,preview}){ShowWindow(controls[id],SW_HIDE);if(labels.contains(id))ShowWindow(labels[id],SW_HIDE);}
        auto showField=[&](int id,int x,int y,int width_){ShowWindow(controls[id],SW_SHOW);ShowWindow(labels[id],SW_SHOW);fieldAt(id,x,y,width_);};
        auto a=options.action;bool video=a==Action::compress||a==Action::h264||a==Action::h265||a==Action::telegram;
        if(video){showField(quality,margin,388,col);showField(width,margin+col+gap,388,col);showField(fps,margin+2*(col+gap),388,col);showField(target,margin,450,col+50);ShowWindow(controls[mute],SW_SHOW);place(mute,margin+col+gap+60,476,2*col-60);}
        if(a==Action::image){showField(format,margin,388,col);showField(width,margin+col+gap,388,col);showField(quality,margin+2*(col+gap),388,col);ShowWindow(controls[firstFrame],SW_SHOW);place(firstFrame,margin,466,w-2*margin);}
        if(a==Action::pdf){ShowWindow(controls[firstFrame],SW_SHOW);place(firstFrame,margin,405,w-2*margin);}
        if(a==Action::rename){for(int i=0;i<6;++i)showField(renameFields[i].id,margin+(i%3)*(col+gap),388+(i/3)*62,col);ShowWindow(controls[preview],SW_SHOW);place(preview,w-margin-190,516,190);}
        place(hint,margin,554,w-2*margin,58);place(progress,margin,618,w-2*margin,12);place(status,margin,636,w-2*margin,24);
        place(run,margin,670,120);place(cancelButton,margin+132,670,100);place(open,margin+244,670,145);place(reveal,margin+401,670,170);
        // Settings use a separate standard dialog, keeping the file workflow compact.
        for(int id:{lang,theme}){ShowWindow(controls[id],SW_HIDE);ShowWindow(labels[id],SW_HIDE);}
        ShowWindow(controls[retryOffice],SW_HIDE);ShowWindow(controls[undo],SW_HIDE);
        auto menu=GetMenu(hwnd);if(menu)DrawMenuBar(hwnd);
    }
    void readOptions(){
        options.action=selected();options.output=text(output);options.quality=number(quality);options.width=number(width);options.fps=number(fps);options.targetMB=number(target);
        options.format=text(format);options.mute=SendMessageW(controls[mute],BM_GETCHECK,0,0)==BST_CHECKED;options.firstFrame=SendMessageW(controls[firstFrame],BM_GETCHECK,0,0)==BST_CHECKED;
        options.pattern=text(pattern);options.prefix=text(prefix);options.suffix=text(suffix);options.find=text(find);options.replace=text(replace);options.start=number(start);
    }
    void chooseFiles(bool folder){
        ComPtr<IFileOpenDialog> dialog;check(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog)));
        DWORD flags;check(dialog->GetOptions(&flags));check(dialog->SetOptions(flags|FOS_FORCEFILESYSTEM|(folder?FOS_PICKFOLDERS:FOS_ALLOWMULTISELECT|FOS_FILEMUSTEXIST)));
        if(dialog->Show(hwnd)==HRESULT_FROM_WIN32(ERROR_CANCELLED))return;
        ComPtr<IShellItemArray> items;check(dialog->GetResults(&items));DWORD count=0;check(items->GetCount(&count));
        for(DWORD i=0;i<count;++i){ComPtr<IShellItem> item;check(items->GetItemAt(i,&item));PWSTR value=nullptr;check(item->GetDisplayName(SIGDN_FILESYSPATH,&value));fs::path path(value);CoTaskMemFree(value);if(folder)text(output,path.wstring());else append(path);}
        if(!folder){refreshFiles();options.action=Action::more;refreshActions();}
    }
    void append(const fs::path& path){if(std::find(options.files.begin(),options.files.end(),path)==options.files.end())options.files.push_back(path);}
    void startWork(){
        readOptions();validate(options);if(options.action==Action::rename&&!previewed)throw std::runtime_error(utf8(tr("previewRequired")));
        busy=true;cancelled=false;fractions.assign(options.files.size(),0);outputs.assign(options.files.size(),{});text(status,tr("working"));
        for(const auto& [id,h]:controls)if(id!=files)EnableWindow(h,id==cancelButton);EnableMenuItem(GetMenu(hwnd),0,MF_BYPOSITION|MF_GRAYED);
        worker=std::thread([this,job=options]{
            HRESULT initialized=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);Result result;
            try{check(initialized);result=execute(job,cancelled,[this](const Update& update){PostMessageW(hwnd,updated,0,reinterpret_cast<LPARAM>(new Update(update)));});}
            catch(const Cancelled&){result.cancelled=true;}
            catch(const std::exception& e){result.failed=1;PostMessageW(hwnd,updated,0,reinterpret_cast<LPARAM>(new Update{0,1,wide(e.what()),{}}));}
            if(SUCCEEDED(initialized))CoUninitialize();PostMessageW(hwnd,finished,0,reinterpret_cast<LPARAM>(new Result(result)));
        });
    }
    void onUpdate(const Update& update){
        if(update.index<options.files.size()){
            auto value=update.status;ListView_SetItemText(controls[files],static_cast<int>(update.index),1,value.data());fractions[update.index]=update.progress;if(!update.output.empty())outputs[update.index]=update.output;
        }
        double total=0;for(auto f:fractions)total+=f;SendMessageW(controls[progress],PBM_SETPOS,fractions.empty()?0:static_cast<WPARAM>(total/fractions.size()*1000),0);
        text(status,update.status);
    }
    void onFinished(const Result& result){
        if(worker.joinable())worker.join();busy=false;
        for(const auto& [id,h]:controls)EnableWindow(h,id!=cancelButton);EnableMenuItem(GetMenu(hwnd),0,MF_BYPOSITION|MF_ENABLED);
        bool any=std::any_of(outputs.begin(),outputs.end(),[](const auto& p){return !p.empty();});EnableWindow(controls[open],any);EnableWindow(controls[reveal],any);
        if(options.action==Action::rename&&result.failed==0&&!result.cancelled)options.files=result.outputs;
        text(status,(result.cancelled?tr("cancelled"):tr("done"))+L"  ·  "+std::to_wstring(result.succeeded)+L"  ·  "+tr("failed")+L": "+std::to_wstring(result.failed));
        refreshActions();if(closeAfter)DestroyWindow(hwnd);
    }
    void rebuildMenu(){
        auto old=GetMenu(hwnd);HMENU menu=CreateMenu(),settings=CreatePopupMenu(),languages=CreatePopupMenu(),themes=CreatePopupMenu();
        int i=0;for(const auto* name:{L"English",L"Русский",L"Deutsch",L"Français",L"简体中文"})AppendMenuW(languages,MF_STRING,500+i++,name);
        i=0;for(auto key:{"system","light","dark"})AppendMenuW(themes,MF_STRING,510+i++,tr(key).c_str());
        AppendMenuW(settings,MF_POPUP,reinterpret_cast<UINT_PTR>(languages),tr("language").c_str());AppendMenuW(settings,MF_POPUP,reinterpret_cast<UINT_PTR>(themes),tr("theme").c_str());
        AppendMenuW(settings,MF_STRING,retryOffice,tr("retryOffice").c_str());AppendMenuW(settings,MF_STRING,undo,tr("undo").c_str());
        AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(settings),L"Context Actions");SetMenu(hwnd,menu);if(old)DestroyMenu(old);
    }
    void command(int id,int notification){
        if(busy){if(id==cancelButton){cancelled=true;text(status,tr("cancelling"));}return;}
        if(id>=500&&id<505){static const wchar_t* languages[]{L"en",L"ru",L"de",L"fr",L"zh-CN"};setLanguage(languages[id-500]);
            // Recreate native controls to apply all translated labels while preserving inputs.
            readOptions();for(auto [key,h]:controls)DestroyWindow(h);for(auto [key,h]:labels)DestroyWindow(h);controls.clear();labels.clear();init();rebuildMenu();return;}
        if(id>=510&&id<513){SendMessageW(controls[theme],CB_SETCURSEL,id-510,0);fs::create_directories(dataDirectory());WritePrivateProfileStringW(L"ui",L"theme",std::to_wstring(id-510).c_str(),(dataDirectory()/L"settings.ini").c_str());applyTheme();return;}
        if(notification==EN_CHANGE){previewed=false;return;}
        if(id==action&&notification==CBN_SELCHANGE){updateAction();return;}
        if(id==add)chooseFiles(false);else if(id==browse)chooseFiles(true);
        else if(id==remove){for(int i=static_cast<int>(options.files.size())-1;i>=0;--i)if(ListView_GetItemState(controls[files],i,LVIS_SELECTED))options.files.erase(options.files.begin()+i);refreshFiles();refreshActions();}
        else if(id==up||id==down){int index=ListView_GetNextItem(controls[files],-1,LVNI_SELECTED),other=index+(id==up?-1:1);if(index>=0&&other>=0&&other<static_cast<int>(options.files.size())){std::swap(options.files[index],options.files[other]);refreshFiles();ListView_SetItemState(controls[files],other,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);}}
        else if(id==preview){readOptions();validate(options);auto names=previewRename(options);for(size_t i=0;i<names.size();++i){auto name=names[i].to.filename().wstring();ListView_SetItemText(controls[files],static_cast<int>(i),1,name.data());}previewed=true;}
        else if(id==run)startWork();
        else if(id==retryOffice){resetOfficeCapabilities();refreshActions();text(status,tr("ready"));}
        else if(id==undo){std::atomic_bool cancel=false;undoRename(cancel);options.files.clear();outputs.clear();refreshFiles();refreshActions();text(status,tr("done"));}
        else if(id==open||id==reveal){int i=ListView_GetNextItem(controls[files],-1,LVNI_SELECTED);auto chosen=i>=0&&i<static_cast<int>(outputs.size())?outputs[i]:fs::path();if(chosen.empty()){auto it=std::find_if(outputs.begin(),outputs.end(),[](const auto& p){return !p.empty();});if(it!=outputs.end())chosen=*it;}if(chosen.empty())return;
            if(id==open)ShellExecuteW(hwnd,L"open",chosen.c_str(),nullptr,nullptr,SW_SHOWNORMAL);
            else{PIDLIST_ABSOLUTE pidl=nullptr;check(SHParseDisplayName(chosen.c_str(),nullptr,&pidl,0,nullptr));HRESULT hr=SHOpenFolderAndSelectItems(pidl,0,nullptr,0);CoTaskMemFree(pidl);check(hr);}}
    }
};
LRESULT CALLBACK procedure(HWND hwnd,UINT message,WPARAM wp,LPARAM lp){
    auto* self=reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    try {
        if(message==WM_NCCREATE){self=static_cast<Window*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);self->hwnd=hwnd;SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(!self)return DefWindowProcW(hwnd,message,wp,lp);
        switch(message){
        case WM_CREATE:self->init();self->rebuildMenu();return 0;
        case WM_SIZE:self->layout();return 0;
        case WM_COMMAND:self->command(LOWORD(wp),HIWORD(wp));return 0;
        case WM_VSCROLL:{int delta=LOWORD(wp)==SB_LINEUP?-30:LOWORD(wp)==SB_LINEDOWN?30:LOWORD(wp)==SB_PAGEUP?-200:LOWORD(wp)==SB_PAGEDOWN?200:0;if(LOWORD(wp)==SB_THUMBTRACK){SCROLLINFO info{sizeof(info),SIF_TRACKPOS};GetScrollInfo(hwnd,SB_VERT,&info);self->scroll=info.nTrackPos;}else self->scroll+=delta;self->layout();return 0;}
        case WM_MOUSEWHEEL:self->scroll-=GET_WHEEL_DELTA_WPARAM(wp)/WHEEL_DELTA*60;self->layout();return 0;
        case WM_DPICHANGED:{self->dpi=HIWORD(wp);auto* rect=reinterpret_cast<RECT*>(lp);SetWindowPos(hwnd,nullptr,rect->left,rect->top,rect->right-rect->left,rect->bottom-rect->top,SWP_NOZORDER);self->applyTheme();self->layout();return 0;}
        case WM_SETTINGCHANGE:self->applyTheme();return 0;
        case WM_GETMINMAXINFO:reinterpret_cast<MINMAXINFO*>(lp)->ptMinTrackSize={self->px(720),self->px(420)};return 0;
        case WM_CTLCOLORSTATIC:case WM_CTLCOLOREDIT:case WM_CTLCOLORLISTBOX:{auto dc=reinterpret_cast<HDC>(wp);SetTextColor(dc,self->fg);SetBkColor(dc,self->bg);return reinterpret_cast<LRESULT>(self->background);}
        case WM_ERASEBKGND:{RECT rect;GetClientRect(hwnd,&rect);FillRect(reinterpret_cast<HDC>(wp),&rect,self->background);return 1;}
        case WM_DROPFILES:{HDROP drop=reinterpret_cast<HDROP>(wp);if(!self->busy){UINT count=DragQueryFileW(drop,0xFFFFFFFF,nullptr,0);for(UINT i=0;i<count;++i){UINT n=DragQueryFileW(drop,i,nullptr,0);std::wstring path(n+1,L'\0');DragQueryFileW(drop,i,path.data(),n+1);path.resize(n);self->append(path);}self->refreshFiles();self->options.action=Action::more;self->refreshActions();}DragFinish(drop);return 0;}
        case updated:{std::unique_ptr<Update> update(reinterpret_cast<Update*>(lp));self->onUpdate(*update);return 0;}
        case finished:{std::unique_ptr<Result> result(reinterpret_cast<Result*>(lp));self->onFinished(*result);return 0;}
        case autorun:self->startWork();return 0;
        case WM_CLOSE:if(self->busy){self->closeAfter=true;self->cancelled=true;self->text(status,tr("cancelling"));}else DestroyWindow(hwnd);return 0;
        case WM_DESTROY:PostQuitMessage(0);return 0;
        }
    }catch(const std::exception& e){if(self&&self->smoke){self->error=e.what();PostMessageW(hwnd,WM_CLOSE,0,0);}else MessageBoxW(hwnd,wide(e.what()).c_str(),L"Context Actions",MB_OK|MB_ICONERROR);}
    return DefWindowProcW(hwnd,message,wp,lp);
}
}
int runUi(Options options,bool immediate,bool smoke){
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_LISTVIEW_CLASSES|ICC_PROGRESS_CLASS|ICC_STANDARD_CLASSES};InitCommonControlsEx(&controls);
    WNDCLASSEXW klass{sizeof(klass)};klass.lpfnWndProc=procedure;klass.hInstance=GetModuleHandleW(nullptr);klass.hIcon=LoadIconW(klass.hInstance,MAKEINTRESOURCEW(101));klass.hIconSm=klass.hIcon;klass.hCursor=LoadCursorW(nullptr,IDC_ARROW);klass.lpszClassName=L"ContextActions.Window";RegisterClassExW(&klass);
    Window window;window.options=std::move(options);window.smoke=smoke;
    RECT work;SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);int dpi=static_cast<int>(GetDpiForSystem());
    auto hwnd=CreateWindowExW(WS_EX_CONTROLPARENT,klass.lpszClassName,L"Context Actions",WS_OVERLAPPEDWINDOW|WS_VSCROLL,CW_USEDEFAULT,CW_USEDEFAULT,std::min<LONG>(MulDiv(1020,dpi,96),work.right-work.left),std::min<LONG>(MulDiv(815,dpi,96),work.bottom-work.top),nullptr,nullptr,klass.hInstance,&window);
    if(!hwnd)check(HRESULT_FROM_WIN32(GetLastError()));
    if(smoke){
        if(SendMessageW(window.controls[action],CB_GETCOUNT,0,0)!=static_cast<LRESULT>(actions.size()))window.error="UI action catalog missing";
        SendMessageW(window.controls[action],CB_SETCURSEL,4,0);SendMessageW(hwnd,WM_COMMAND,MAKEWPARAM(action,CBN_SELCHANGE),0);
        if(window.options.action!=Action::image)window.error="UI action selection failed";
        PostMessageW(hwnd,WM_CLOSE,0,0);
    }else ShowWindow(hwnd,SW_SHOWNORMAL);
    if(immediate)PostMessageW(hwnd,autorun,0,0);
    MSG message;while(GetMessageW(&message,nullptr,0,0)>0){if(!IsDialogMessageW(hwnd,&message)){TranslateMessage(&message);DispatchMessageW(&message);}}
    if(!window.error.empty())throw std::runtime_error(window.error);return 0;
}
}
