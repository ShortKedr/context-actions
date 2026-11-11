#include "pch.h"
#include "MainWindow.xaml.h"
#include "MainWindow.g.cpp"
#include <microsoft.ui.xaml.window.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <shellapi.h>
#include <cmath>
#include <numeric>

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace winrt::Microsoft::UI::Xaml::Media;
using namespace winrt::Microsoft::UI::Xaml::Automation;
using namespace winrt::Microsoft::UI::Windowing;

namespace {
Visibility visible(bool value){return value?Visibility::Visible:Visibility::Collapsed;}
auto label(char const* key){return box_value(ca::tr(key));}
bool video(ca::Action a){return a==ca::Action::compress||a==ca::Action::h264||a==ca::Action::h265||a==ca::Action::telegram;}
int category(ca::Action a){return video(a)?0:a==ca::Action::image?1:a==ca::Action::pdf?2:a==ca::Action::rename?3:4;}
void describe(FrameworkElement const& control,char const* key){
    AutomationProperties::SetName(control,ca::tr(key));ToolTipService::SetToolTip(control,label(key));
}
void errorDetails(Button const& button,std::wstring const& message){
    button.Visibility(visible(!message.empty()));
    if(message.empty()){button.Flyout(nullptr);return;}
    TextBox text;text.AcceptsReturn(true);text.TextWrapping(TextWrapping::Wrap);text.IsReadOnly(true);text.Text(message);text.FontSize(12);text.MaxWidth(420);text.MaxHeight(280);describe(text,"errorDetails");AutomationProperties::SetAutomationId(text,L"ErrorText");
    ScrollViewer::SetVerticalScrollBarVisibility(text,ScrollBarVisibility::Auto);ScrollViewer::SetHorizontalScrollBarVisibility(text,ScrollBarVisibility::Disabled);
    Flyout flyout;flyout.Content(text);button.Flyout(flyout);
}
template<class T> [[noreturn]] void fieldError(T const& box,std::wstring const& message){
    TextBlock text;text.Text(message);text.TextWrapping(TextWrapping::Wrap);text.FontSize(12);
    AutomationProperties::SetLiveSetting(text,Peers::AutomationLiveSetting::Polite);AutomationProperties::SetHelpText(box,message);
    text.Loaded([](auto const& sender,auto&&){sender.template as<FrameworkElement>().StartBringIntoView();});
    box.Description(text);box.Focus(FocusState::Programmatic);throw std::runtime_error(ca::utf8(message));
}
template<class T> bool clearFieldError(T const& box){
    if(!box.Description())return false;box.Description(nullptr);AutomationProperties::SetHelpText(box,L"");return true;
}
std::wstring numberError(NumberBox const& box){return ca::tr("wholeNumberRange")+L" "+std::to_wstring(static_cast<int>(box.Minimum()))+L" – "+std::to_wstring(static_cast<int>(box.Maximum()))+L".";}
int number(NumberBox const& box){
    double value=box.Value();
    if(!std::isfinite(value)||std::floor(value)!=value||value<box.Minimum()||value>box.Maximum()){
        fieldError(box,numberError(box));
    }
    return static_cast<int>(value);
}
}

namespace winrt::ContextActions::implementation {
MainWindow::MainWindow() {
    InitializeComponent();
    ExtendsContentIntoTitleBar(true);SetTitleBar(AppTitleBar());
    Window window=*this;check_hresult(window.as<IWindowNative>()->get_WindowHandle(&hwnd_));
    auto icon=LoadIconW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(101));
    SendMessageW(hwnd_,WM_SETICON,ICON_BIG,reinterpret_cast<LPARAM>(icon));
    SendMessageW(hwnd_,WM_SETICON,ICON_SMALL,reinterpret_cast<LPARAM>(icon));
    Root().SizeChanged([this](auto&&,auto&&){Layout();});
    AddButton().Click([this](auto&&,auto&&){Try([&]{ChooseFiles(false);});});
    BrowseButton().Click([this](auto&&,auto&&){Try([&]{ChooseFiles(true);});});
    ResetOutputButton().Click([this](auto&&,auto&&){OutputBox().Text(L"");});
    RemoveButton().Click([this](auto&&,auto&&){Try([&]{RemoveFiles();});});
    UpButton().Click([this](auto&&,auto&&){Try([&]{MoveFile(-1);});});
    DownButton().Click([this](auto&&,auto&&){Try([&]{MoveFile(1);});});
    PreviewButton().Click([this](auto&&,auto&&){Try([&]{Preview();});});
    RunButton().Click([this](auto&&,auto&&){Try([&]{Start();});});
    CancelButton().Click([this](auto&&,auto&&){*cancelled_=true;StatusText().Text(ca::tr("cancelling"));CancelButton().IsEnabled(false);});
    UndoButton().Click([this](auto&&,auto&&){Try([&]{std::atomic_bool cancel=false;ca::undoRename(cancel);options_.files.clear();RefreshFiles();RefreshActions(false);StatusText().Text(ca::tr("done"));});});
    OpenButton().Click([this](auto&&,auto&&){Try([&]{OpenResult(false);});});
    RevealButton().Click([this](auto&&,auto&&){Try([&]{OpenResult(true);});});
    SettingsButton().Click([this](auto&&,auto&&){Try([&]{if(!settings_)ReadOptions();ShowSettings(!settings_);});});
    BackButton().Click([this](auto&&,auto&&){ShowSettings(false);});
    ExpandButton().Click([this](auto&&,auto&&){Try([&]{ReadOptions();compact_=false;ExpandButton().Visibility(Visibility::Collapsed);AppWindow().Resize({MulDiv(1040,GetDpiForWindow(hwnd_),96),MulDiv(760,GetDpiForWindow(hwnd_),96)});ActionChanged();Layout();});});
    OfficeButton().Click([this](auto&&,auto&&){Try([&]{ca::resetOfficeCapabilities();RefreshActions(false);MessageBar().Severity(InfoBarSeverity::Success);MessageBar().Message(ca::tr("ready"));MessageBar().IsOpen(true);});});
    CategoryBox().SelectionChanged([this](auto&&,auto&&){if(!updating_)Try([&]{ReadOptions();RefreshActions(false);});});
    ActionBox().SelectionChanged([this](auto&&,auto&&){if(!updating_)Try([&]{ReadOptions();auto i=ActionBox().SelectedIndex();if(i>=0){if(options_.action!=visibleActions_.at(i))InvalidatePreview();options_.action=visibleActions_.at(i);ActionChanged();}});});
    PresetBox().SelectionChanged([this](auto&&,auto&&){if(!updating_){auto i=PresetBox().SelectedIndex();if(i>=0&&i<3){static const int crf[]{18,24,30};QualityBox().Value(crf[i]);}}});
    QualityBox().ValueChanged([this](auto&&,auto&&){if(!updating_){updating_=true;auto q=QualityBox().Value();PresetBox().SelectedIndex(q==18?0:q==24?1:q==30?2:3);updating_=false;}});
    for(auto const& box:{PatternBox(),PrefixBox(),SuffixBox(),FindBox(),ReplaceBox()})box.TextChanged([this](auto&&,auto&&){if(!updating_)InvalidatePreview();});
    StartBox().ValueChanged([this](auto&&,auto&&){if(!updating_)InvalidatePreview();});
    for(auto const& box:{QualityBox(),WidthBox(),FpsBox(),TargetBox(),ImageQualityBox(),ImageWidthBox(),StartBox()})
        box.ValueChanged([this](auto const& changed,auto&&){if(clearFieldError(changed))MessageBar().IsOpen(false);});
    for(auto const& box:{PatternBox(),OutputBox()})
        box.TextChanged([this](auto const& sender,auto&&){if(clearFieldError(sender.template as<TextBox>()))MessageBar().IsOpen(false);});
    LanguageBox().SelectionChanged([this](auto&&,auto&&){if(!updating_)Try([&]{
        static const wchar_t* codes[]{L"en",L"ru",L"de",L"fr",L"zh-CN"};auto i=LanguageBox().SelectedIndex();if(i<0)return;
        std::vector<std::pair<TextBlock,char const*>> statuses;
        for(auto key:{"ready","working","done","failed","cancelled"}){auto old=ca::tr(key);for(auto const& status:rowStatus_)if(status.Text()==old)statuses.emplace_back(status,key);}
        char const* messageKey=nullptr;for(auto key:{"ready","partialFailure","invalid","previewRequired","widthRange","patternRequired","outputDirectory"})if(MessageBar().Message()==ca::tr(key))messageKey=key;
        std::wstring messageSuffix;if(MessageBar().Message().starts_with(ca::tr("wholeNumberRange"))){messageKey="wholeNumberRange";messageSuffix=std::wstring(MessageBar().Message()).substr(ca::tr(messageKey).size());}
        ca::setLanguage(codes[i]);for(auto const& [status,key]:statuses)status.Text(ca::tr(key));if(messageKey)MessageBar().Message(ca::tr(messageKey)+messageSuffix);
        Localize();RefreshFiles(false);RefreshActions(false);
    });});
    ThemeBox().SelectionChanged([this](auto&&,auto&&){if(!updating_)Try([&]{ca::fs::create_directories(ca::dataDirectory());auto path=ca::dataDirectory()/L"settings.ini";if(!WritePrivateProfileStringW(L"ui",L"theme",std::to_wstring(ThemeBox().SelectedIndex()).c_str(),path.c_str()))ca::check(HRESULT_FROM_WIN32(GetLastError()));ApplyTheme();});});
    Root().DragOver([this](auto&&,DragEventArgs const& args){args.AcceptedOperation(!busy_&&args.DataView().Contains(Windows::ApplicationModel::DataTransfer::StandardDataFormats::StorageItems())?Windows::ApplicationModel::DataTransfer::DataPackageOperation::Copy:Windows::ApplicationModel::DataTransfer::DataPackageOperation::None);});
    Root().Drop([this](auto&&,DragEventArgs const& args){DropFiles(args);});
    AppWindow().Closing([this](auto&&,AppWindowClosingEventArgs const& args){if(busy_){args.Cancel(true);closeAfter_=true;*cancelled_=true;StatusText().Text(ca::tr("cancelling"));}});
}
MainWindow::~MainWindow(){*cancelled_=true;if(worker_.joinable())worker_.join();}

void MainWindow::Configure(ca::Options options,bool immediate) {
    updating_=true;options_=std::move(options);compact_=options_.action!=ca::Action::more&&!options_.files.empty();
    OutputBox().Text(options_.output.wstring());PatternBox().Text(options_.pattern);PrefixBox().Text(options_.prefix);SuffixBox().Text(options_.suffix);FindBox().Text(options_.find);ReplaceBox().Text(options_.replace);StartBox().Value(options_.start);
    for(auto f:{L"png",L"jpg",L"webp",L"bmp",L"tiff",L"gif"})FormatBox().Items().Append(box_value(f));
    for(uint32_t i=0;i<FormatBox().Items().Size();++i)if(unbox_value<hstring>(FormatBox().Items().GetAt(i))==options_.format)FormatBox().SelectedIndex(i);
    for(auto name:{L"English",L"Русский",L"Deutsch",L"Français",L"简体中文"})LanguageBox().Items().Append(box_value(name));
    auto lang=ca::language();LanguageBox().SelectedIndex(lang.starts_with(L"ru")?1:lang.starts_with(L"de")?2:lang.starts_with(L"fr")?3:lang.starts_with(L"zh")?4:0);
    Localize();RefreshFiles();RefreshActions(true);ApplyTheme();updating_=false;
    RECT work{};SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);int dpi=GetDpiForWindow(hwnd_);
    AppWindow().Resize({std::min<LONG>(MulDiv(compact_?620:1040,dpi,96),work.right-work.left),std::min<LONG>(MulDiv(760,dpi,96),work.bottom-work.top)});
    ExpandButton().Visibility(visible(compact_));Layout();
    if(immediate)DispatcherQueue().TryEnqueue([weak=get_weak()]{if(auto self=weak.get())self->Try([&]{self->Start();});});
}

void MainWindow::Localize(){
    bool previous=updating_;updating_=true;
    AddButton().Content(label("add"));PreviewButton().Content(label("preview"));UndoButton().Content(label("undo"));RunButton().Content(label("run"));CancelButton().Content(label("cancel"));
    BrowseButton().Content(label("browse"));ResetOutputButton().Content(label("sameFolder"));OpenButton().Content(label("open"));RevealButton().Content(label("reveal"));BackButton().Content(label("back"));OfficeButton().Content(label("retryOffice"));
    DropTitle().Text(ca::tr("dropTitle"));DropHint().Text(ca::tr("dropHint"));FilesHeading().Text(ca::tr("filesTitle"));
    OutputBox().Header(label("saveTo"));OutputBox().PlaceholderText(ca::tr("sameFolder"));
    QualityBox().Header(label("quality"));WidthBox().Header(label("width"));FpsBox().Header(label("fps"));TargetBox().Header(label("target"));MuteBox().Content(label("mute"));
    ImageQualityBox().Header(label("imageQuality"));ImageWidthBox().Header(label("width"));FormatBox().Header(label("format"));FirstFrameBox().Content(label("firstFrame"));
    PatternBox().Header(label("pattern"));PrefixBox().Header(label("prefix"));SuffixBox().Header(label("suffix"));FindBox().Header(label("find"));ReplaceBox().Header(label("replace"));StartBox().Header(label("start"));
    VideoAdvanced().Header(label("advanced"));ImageAdvanced().Header(label("advanced"));RenameAdvanced().Header(label("advanced"));
    for(auto const& expander:{VideoAdvanced(),ImageAdvanced(),RenameAdvanced()})describe(expander,"advanced");
    CategoryBox().Header(label("category"));ActionBox().Header(label("action"));PresetBox().Header(label("qualityPreset"));LanguageBox().Header(label("language"));ThemeBox().Header(label("theme"));
    auto translateError=[](auto const& box,std::wstring const& message){if(auto text=box.Description().template try_as<TextBlock>()){text.Text(message);AutomationProperties::SetHelpText(box,message);}};
    for(auto const& box:{QualityBox(),WidthBox(),FpsBox(),TargetBox(),ImageQualityBox(),ImageWidthBox(),StartBox()})
        translateError(box,(box==WidthBox()||box==ImageWidthBox())&&box.Value()==1?ca::tr("widthRange"):numberError(box));
    translateError(PatternBox(),ca::tr("patternRequired"));translateError(OutputBox(),ca::tr("outputDirectory"));
    auto selectedCategory=CategoryBox().SelectedIndex();CategoryBox().Items().Clear();for(auto key:{"videoCategory","imageCategory","pdfCategory","renameCategory","officeCategory"})CategoryBox().Items().Append(label(key));CategoryBox().SelectedIndex(selectedCategory<0?0:selectedCategory);
    auto preset=PresetBox().SelectedIndex();PresetBox().Items().Clear();for(auto key:{"highQuality","balanced","smallerFile","custom"})PresetBox().Items().Append(label(key));PresetBox().SelectedIndex(preset<0?1:preset);
    auto theme=std::clamp(GetPrivateProfileIntW(L"ui",L"theme",0,(ca::dataDirectory()/L"settings.ini").c_str()),0u,2u);ThemeBox().Items().Clear();for(auto key:{"system","light","dark"})ThemeBox().Items().Append(label(key));ThemeBox().SelectedIndex(theme);
    describe(SettingsButton(),"settings");describe(ExpandButton(),"fullWorkspace");describe(RemoveButton(),"remove");describe(UpButton(),"up");describe(DownButton(),"down");describe(FileList(),"file");
    if(!busy_)RefreshStatus();
    updating_=previous;
}
void MainWindow::Layout(){
    bool narrow=Root().ActualWidth()<860||compact_;
    ActionSelectors().Visibility(visible(!compact_));
    FilesColumn().Width({1,GridUnitType::Star});OptionsColumn().Width({narrow?0.0:360.0,GridUnitType::Pixel});
    Grid::SetColumn(OperationPane(),narrow?0:1);Grid::SetRow(OperationPane(),narrow?1:0);
    FileList().MaxHeight(compact_?160:440);
    bool narrowFooter=Root().ActualWidth()<560;
    FooterButtonsColumn().Width(narrowFooter?GridLength{0,GridUnitType::Pixel}:GridLength{1,GridUnitType::Auto});
    Grid::SetColumn(FooterActions(),narrowFooter?0:1);Grid::SetRow(FooterActions(),narrowFooter?1:0);
    ResultActions().Orientation(narrowFooter?Orientation::Vertical:Orientation::Horizontal);
}
void MainWindow::RefreshFiles(bool reset){
    std::vector<hstring> statuses;if(!reset)for(auto const& text:rowStatus_)statuses.push_back(text.Text());
    std::vector<uint32_t> selected;if(!reset)for(auto const& item:FileList().SelectedItems()){uint32_t i{};if(FileList().Items().IndexOf(item,i))selected.push_back(i);}
    FileList().Items().Clear();rowStatus_.clear();rowDetails_.clear();
    if(reset)rowErrors_.assign(options_.files.size(),{});
    if(reset){lastResult_={};RefreshStatus();outputs_.assign(options_.files.size(),{});fractions_.assign(options_.files.size(),0);previewed_=false;ResultActions().Visibility(Visibility::Collapsed);OverallProgress().Visibility(Visibility::Collapsed);}
    for(size_t i=0;i<options_.files.size();++i){
        auto const& file=options_.files[i];StackPanel row;row.Spacing(4);row.Padding({0,8,0,8});
        TextBlock name;name.Text(file.filename().wstring());name.TextWrapping(TextWrapping::Wrap);name.FontWeight(Windows::UI::Text::FontWeight{600});
        TextBlock path;path.Text(file.parent_path().wstring());path.FontSize(12);path.TextTrimming(TextTrimming::CharacterEllipsis);
        TextBlock status;status.Text(i<statuses.size()?statuses[i]:hstring(ca::tr("ready")));status.TextWrapping(TextWrapping::Wrap);status.FontSize(12);
        if(!rowErrors_[i].empty())status.Text(ca::tr("failed"));
        Button details;details.Content(label("errorDetails"));details.HorizontalAlignment(HorizontalAlignment::Left);details.Padding({8,4,8,4});describe(details,"errorDetails");AutomationProperties::SetAutomationId(details,L"ErrorDetails");errorDetails(details,rowErrors_[i]);
        row.Children().Append(name);row.Children().Append(path);row.Children().Append(status);row.Children().Append(details);rowStatus_.push_back(status);rowDetails_.push_back(details);
        ToolTipService::SetToolTip(row,box_value(file.wstring()));AutomationProperties::SetName(row,file.wstring());FileList().Items().Append(row);
    }
    for(auto i:selected)if(i<FileList().Items().Size())FileList().SelectedItems().Append(FileList().Items().GetAt(i));
    EmptyState().Visibility(visible(options_.files.empty()));FileList().Visibility(visible(!options_.files.empty()));
    FilesHeading().Text(ca::tr("filesTitle")+L" ("+std::to_wstring(options_.files.size())+L")");
    RemoveButton().IsEnabled(!busy_&&!options_.files.empty());UpButton().IsEnabled(!busy_&&options_.files.size()>1);DownButton().IsEnabled(!busy_&&options_.files.size()>1);
}
void MainWindow::RefreshActions(bool selectBest){
    bool previous=updating_;updating_=true;
    auto previousAction=options_.action;
    auto all=ca::catalog();
    if(selectBest){
        if(options_.action==ca::Action::more){auto best=std::find_if(all.begin(),all.end(),[&](auto a){return ca::unavailableReason(a,options_.files).empty();});options_.action=best==all.end()?ca::Action::compress:*best;}
        CategoryBox().SelectedIndex(category(options_.action));
    }
    visibleActions_.clear();ActionBox().Items().Clear();int chosen=0;
    for(auto a:all)if(category(a)==CategoryBox().SelectedIndex()){if(a==options_.action)chosen=static_cast<int>(visibleActions_.size());visibleActions_.push_back(a);ActionBox().Items().Append(label(ca::actionKey(a)));}
    ActionBox().SelectedIndex(chosen);if(!visibleActions_.empty())options_.action=visibleActions_[chosen];
    if(options_.action!=previousAction)InvalidatePreview();
    updating_=previous;ActionChanged();
}
void MainWindow::ActionChanged(){
    bool previous=updating_;updating_=true;auto a=options_.action;
    VideoOptions().Visibility(visible(video(a)));ImageOptions().Visibility(visible(a==ca::Action::image));RenameOptions().Visibility(visible(a==ca::Action::rename));FirstFrameBox().Visibility(visible(a==ca::Action::image||a==ca::Action::pdf));OutputOptions().Visibility(visible(a!=ca::Action::rename));
    QualityBox().Value(options_.quality);ImageQualityBox().Value(options_.quality);WidthBox().Value(options_.width);ImageWidthBox().Value(options_.width);FpsBox().Value(options_.fps);TargetBox().Value(options_.targetMB);MuteBox().IsChecked(options_.mute);FirstFrameBox().IsChecked(options_.firstFrame);
    PresetBox().SelectedIndex(options_.quality==18?0:options_.quality==24?1:options_.quality==30?2:3);
    auto reason=ca::unavailableReason(a,options_.files);Availability().Message(reason);Availability().IsOpen(!reason.empty());RunButton().IsEnabled(!busy_&&reason.empty()&&!settings_);
    Hint().Text(ca::tr(a==ca::Action::image?"imageHint":a==ca::Action::pdf?"pdfHint":a==ca::Action::rename?"renameHint":category(a)==4?"officeHint":"videoHint"));
    if(settings_){Heading().Text(ca::tr("settings"));Subtitle().Text(ca::tr("settingsHint"));}
    else{Heading().Text(compact_?ca::tr(ca::actionKey(a)):L"Context Actions");Subtitle().Text(compact_?std::to_wstring(options_.files.size())+L" · "+ca::tr("filesTitle"):ca::tr("tagline"));}
    updating_=previous;
}
void MainWindow::ReadOptions(){
    auto a=options_.action;auto next=options_;
    next.output=std::wstring(OutputBox().Text());
    if(video(a)){next.quality=number(QualityBox());next.width=number(WidthBox());next.fps=number(FpsBox());next.targetMB=number(TargetBox());next.mute=MuteBox().IsChecked().Value();}
    if(a==ca::Action::image){next.quality=number(ImageQualityBox());next.width=number(ImageWidthBox());next.format=std::wstring(unbox_value<hstring>(FormatBox().SelectedItem()));}
    if(a==ca::Action::image||a==ca::Action::pdf)next.firstFrame=FirstFrameBox().IsChecked().Value();
    if(a==ca::Action::rename){next.pattern=std::wstring(PatternBox().Text());next.prefix=std::wstring(PrefixBox().Text());next.suffix=std::wstring(SuffixBox().Text());next.find=std::wstring(FindBox().Text());next.replace=std::wstring(ReplaceBox().Text());next.start=number(StartBox());}
    options_=std::move(next);
}
void MainWindow::ValidateOptions(){
    ReadOptions();auto a=options_.action;
    if((video(a)||a==ca::Action::image)&&options_.width==1)fieldError(a==ca::Action::image?ImageWidthBox():WidthBox(),ca::tr("widthRange"));
    if(a==ca::Action::rename&&options_.pattern.empty())fieldError(PatternBox(),ca::tr("patternRequired"));
    if(a!=ca::Action::rename&&!options_.output.empty()&&ca::fs::exists(options_.output)&&!ca::fs::is_directory(options_.output))fieldError(OutputBox(),ca::tr("outputDirectory"));
    ca::validate(options_);
}
void MainWindow::ChooseFiles(bool folder){
    if(busy_)return;ca::ComPtr<IFileOpenDialog> dialog;ca::check(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog)));
    DWORD flags{};ca::check(dialog->GetOptions(&flags));ca::check(dialog->SetOptions(flags|FOS_FORCEFILESYSTEM|(folder?FOS_PICKFOLDERS:FOS_ALLOWMULTISELECT|FOS_FILEMUSTEXIST)));
    auto hr=dialog->Show(hwnd_);if(hr==HRESULT_FROM_WIN32(ERROR_CANCELLED))return;ca::check(hr);
    ca::ComPtr<IShellItemArray> items;ca::check(dialog->GetResults(&items));DWORD count{};ca::check(items->GetCount(&count));std::vector<ca::fs::path> paths;
    for(DWORD i=0;i<count;++i){ca::ComPtr<IShellItem> item;ca::check(items->GetItemAt(i,&item));PWSTR path{};ca::check(item->GetDisplayName(SIGDN_FILESYSPATH,&path));paths.emplace_back(path);CoTaskMemFree(path);}
    if(folder){if(!paths.empty())OutputBox().Text(paths.front().wstring());}else Append(paths);
}
void MainWindow::Append(std::vector<ca::fs::path> const& paths){
    if(busy_)return;ReadOptions();for(auto const& path:paths){if(!ca::fs::is_regular_file(path))throw std::runtime_error(ca::utf8(ca::tr("missing")+path.wstring()));}
    for(auto const& path:paths)if(std::find(options_.files.begin(),options_.files.end(),path)==options_.files.end())options_.files.push_back(path);
    if(!ca::accepts(options_.action,options_.files))options_.action=ca::Action::more;RefreshFiles();RefreshActions(true);
}
fire_and_forget MainWindow::DropFiles(DragEventArgs args){
    auto lifetime=get_strong();auto deferral=args.GetDeferral();
    try {if(!busy_&&args.DataView().Contains(Windows::ApplicationModel::DataTransfer::StandardDataFormats::StorageItems())){auto items=co_await args.DataView().GetStorageItemsAsync();std::vector<ca::fs::path> paths;for(auto const& item:items)paths.emplace_back(std::wstring(item.Path()));if(IsWindow(hwnd_))Append(paths);}}
    catch(hresult_error const& e){ShowError(std::wstring(e.message()));}catch(std::exception const& e){ShowError(ca::wide(e.what()));}
    deferral.Complete();
}
void MainWindow::RemoveFiles(){
    if(busy_)return;ReadOptions();std::vector<uint32_t> indices;for(auto const& item:FileList().SelectedItems()){uint32_t i{};if(FileList().Items().IndexOf(item,i))indices.push_back(i);}
    std::sort(indices.rbegin(),indices.rend());for(auto i:indices)options_.files.erase(options_.files.begin()+i);RefreshFiles();RefreshActions(false);
}
void MainWindow::MoveFile(int direction){
    if(busy_||FileList().SelectedItems().Size()!=1)return;int i=FileList().SelectedIndex(),other=i+direction;
    if(i>=0&&other>=0&&other<static_cast<int>(options_.files.size())){std::swap(options_.files[i],options_.files[other]);RefreshFiles();FileList().SelectedIndex(other);}
}
void MainWindow::InvalidatePreview(){
    if(previewed_){previewed_=false;for(auto const& status:rowStatus_)status.Text(ca::tr("ready"));}
}
void MainWindow::Preview(){
    ValidateOptions();auto names=ca::previewRename(options_);
    for(size_t i=0;i<names.size();++i){rowStatus_[i].Text(L"→ "+names[i].to.filename().wstring());rowErrors_[i].clear();errorDetails(rowDetails_[i],{});}previewed_=true;MessageBar().IsOpen(false);
}
void MainWindow::ShowError(std::wstring const& message){MessageBar().Severity(InfoBarSeverity::Error);MessageBar().Message(message);MessageBar().IsOpen(true);}
void MainWindow::SetBusy(bool busy){
    busy_=busy;OperationPane().IsEnabled(!busy);AddButton().IsEnabled(!busy);RemoveButton().IsEnabled(!busy);UpButton().IsEnabled(!busy);DownButton().IsEnabled(!busy);SettingsButton().IsEnabled(!busy);ExpandButton().IsEnabled(!busy);CancelButton().IsEnabled(busy);RunButton().IsEnabled(!busy&&!settings_&&ca::unavailableReason(options_.action,options_.files).empty());
}
void MainWindow::Start(){
    if(busy_)return;ValidateOptions();if(options_.action==ca::Action::rename&&!previewed_)throw std::runtime_error(ca::utf8(ca::tr("previewRequired")));
    if(worker_.joinable())worker_.join();SetBusy(true);lastResult_={};*cancelled_=false;closeAfter_=false;MessageBar().IsOpen(false);ResultActions().Visibility(Visibility::Collapsed);fractions_.assign(options_.files.size(),0);outputs_.assign(options_.files.size(),{});OverallProgress().Value(0);OverallProgress().Visibility(Visibility::Visible);StatusText().Text(ca::tr("working"));
    for(size_t i=0;i<rowErrors_.size();++i){rowErrors_[i].clear();errorDetails(rowDetails_[i],{});rowStatus_[i].Text(ca::tr("ready"));}
    auto queue=DispatcherQueue();auto weak=get_weak();auto cancel=cancelled_;
    try {worker_=std::thread([weak,queue,cancel,job=options_]{
        HRESULT com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);ca::Result result;
        auto update=[weak,queue](ca::Update const& value){queue.TryEnqueue([weak,value]{if(auto self=weak.get())self->OnUpdate(value);});};
        try{ca::check(com);result=ca::execute(job,*cancel,update);}
        catch(ca::Cancelled const&){result.cancelled=true;}
        catch(std::exception const& e){result.failed=1;update({0,1,ca::wide(e.what()),{},true});}
        if(SUCCEEDED(com))CoUninitialize();queue.TryEnqueue([weak,result]{if(auto self=weak.get())self->Finish(result);});
    });}catch(...){SetBusy(false);throw;}
}
void MainWindow::OnUpdate(ca::Update const& update){
    if(update.index>=rowStatus_.size())return;rowStatus_[update.index].Text(update.failed?ca::tr("failed"):update.status);rowErrors_[update.index]=update.failed?update.status:L"";errorDetails(rowDetails_[update.index],rowErrors_[update.index]);fractions_[update.index]=update.progress;if(!update.output.empty())outputs_[update.index]=update.output;
    double sum=std::accumulate(fractions_.begin(),fractions_.end(),0.0);OverallProgress().Value(fractions_.empty()?0:100*sum/fractions_.size());
}
void MainWindow::Finish(ca::Result const& result){
    if(worker_.joinable())worker_.join();SetBusy(false);previewed_=false;
    if(options_.action==ca::Action::rename&&result.failed==0&&!result.cancelled){options_.files=result.outputs;outputs_=result.outputs;RefreshFiles(false);}
    bool any=std::any_of(outputs_.begin(),outputs_.end(),[](auto const& p){return !p.empty();});ResultActions().Visibility(visible(any));
    lastResult_={result.succeeded,result.failed,result.cancelled,{}};RefreshStatus();
    if(!result.cancelled&&result.failed==0)OverallProgress().Value(100);
    if(result.failed){MessageBar().Severity(InfoBarSeverity::Warning);MessageBar().Message(ca::tr("partialFailure"));MessageBar().IsOpen(true);}
    if(closeAfter_)Close();
}
void MainWindow::RefreshStatus(){
    auto const& result=lastResult_;
    if(!result.succeeded&&!result.failed&&!result.cancelled){StatusText().Text(ca::tr("ready"));return;}
    StatusText().Text(ca::tr(result.cancelled?"cancelled":"done")+L" · "+std::to_wstring(result.succeeded)+L" "+ca::tr("succeeded")+L" · "+std::to_wstring(result.failed)+L" "+ca::tr("failed"));
}
void MainWindow::ShowSettings(bool show){
    settings_=show;BodyScroll().Visibility(visible(!show));SettingsScroll().Visibility(visible(show));RunButton().IsEnabled(!show&&!busy_&&ca::unavailableReason(options_.action,options_.files).empty());
    if(show){Heading().Text(ca::tr("settings"));Subtitle().Text(ca::tr("settingsHint"));BackButton().Focus(FocusState::Programmatic);}else{ActionChanged();SettingsButton().Focus(FocusState::Programmatic);}
}
void MainWindow::ApplyTheme(){
    auto selected=ThemeBox().SelectedIndex();Root().RequestedTheme(selected==1?ElementTheme::Light:selected==2?ElementTheme::Dark:ElementTheme::Default);
    AppWindow().TitleBar().PreferredTheme(selected==1?TitleBarTheme::Light:selected==2?TitleBarTheme::Dark:TitleBarTheme::UseDefaultAppMode);
}
void MainWindow::OpenResult(bool reveal){
    auto index=FileList().SelectedIndex();ca::fs::path chosen=index>=0&&index<static_cast<int>(outputs_.size())?outputs_[index]:ca::fs::path{};
    if(chosen.empty()){auto it=std::find_if(outputs_.begin(),outputs_.end(),[](auto const& path){return !path.empty();});if(it!=outputs_.end())chosen=*it;}if(chosen.empty())return;
    if(reveal){PIDLIST_ABSOLUTE pidl{};ca::check(SHParseDisplayName(chosen.c_str(),nullptr,&pidl,0,nullptr));auto hr=SHOpenFolderAndSelectItems(pidl,0,nullptr,0);CoTaskMemFree(pidl);ca::check(hr);}
    else if(reinterpret_cast<INT_PTR>(ShellExecuteW(hwnd_,L"open",chosen.c_str(),nullptr,nullptr,SW_SHOWNORMAL))<=32)throw std::runtime_error(ca::utf8(ca::tr("failed")));
}
}
