#pragma once
#include "MainWindow.g.h"
#include <memory>

namespace winrt::ContextActions::implementation {
struct MainWindow : MainWindowT<MainWindow> {
    MainWindow();
    ~MainWindow();
    void Configure(ca::Options options,bool immediate);
private:
    ca::Options options_;
    ca::Result lastResult_;
    std::vector<ca::Action> visibleActions_;
    std::vector<ca::fs::path> outputs_;
    std::vector<double> fractions_;
    std::vector<Microsoft::UI::Xaml::Controls::TextBlock> rowStatus_;
    std::vector<Microsoft::UI::Xaml::Controls::Button> rowDetails_;
    std::vector<std::wstring> rowErrors_;
    std::thread worker_;
    std::shared_ptr<std::atomic_bool> cancelled_=std::make_shared<std::atomic_bool>(false);
    HWND hwnd_=nullptr;
    bool busy_=false, updating_=false, compact_=false, settings_=false, previewed_=false, closeAfter_=false;
    void Localize();
    void Layout();
    void RefreshFiles(bool reset=true);
    void RefreshActions(bool selectBest);
    void ActionChanged();
    void RefreshArchiveFormats(std::wstring const& preferred);
    Microsoft::UI::Xaml::Controls::ComboBox FormatPicker(ca::Action action);
    void ReadOptions();
    void ValidateOptions();
    void ChooseFiles(bool folder);
    void Append(std::vector<ca::fs::path> const& paths);
    void RemoveFiles();
    void MoveFile(int direction);
    void Preview();
    void InvalidatePreview();
    void Start();
    void OnUpdate(ca::Update const& update);
    void Finish(ca::Result const& result);
    void RefreshStatus();
    void SetBusy(bool busy);
    void ShowError(std::wstring const& message);
    void ShowSettings(bool show);
    void ApplyTheme();
    void OpenResult(bool reveal);
    winrt::fire_and_forget DropFiles(Microsoft::UI::Xaml::DragEventArgs args);
    template<class F> void Try(F&& action) {
        try { action(); }
        catch(winrt::hresult_error const& e) { ShowError(std::wstring(e.message())); }
        catch(std::exception const& e) { ShowError(ca::wide(e.what())); }
    }
};
}
namespace winrt::ContextActions::factory_implementation {
struct MainWindow : MainWindowT<MainWindow,implementation::MainWindow> {};
}
