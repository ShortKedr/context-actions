#pragma once
#include "App.xaml.g.h"
#include <winrt/Microsoft.Windows.ApplicationModel.Resources.h>

namespace winrt::ContextActions::implementation {
struct App : AppT<App> {
    App();
    void OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&);
    Microsoft::UI::Xaml::Window window{nullptr};
    Microsoft::Windows::ApplicationModel::Resources::ResourceManager resources{nullptr};
private:
    void ShowWorkspace(ca::Options options, bool immediate);
    void ShowActionChooser();
};
}
