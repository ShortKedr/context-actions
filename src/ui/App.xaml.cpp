#include "pch.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"
#include <fstream>

namespace {
ca::Options launchOptions;
bool launchImmediate=false;
}
namespace winrt::ContextActions::implementation {
App::App() {
    UnhandledException([](auto&&, Microsoft::UI::Xaml::UnhandledExceptionEventArgs const& args) {
        try {
            ca::fs::create_directories(ca::dataDirectory());
            std::ofstream(ca::dataDirectory()/L"ui-error.txt") << std::hex << args.Exception().value << "\n" << winrt::to_string(args.Message());
        } catch(...) {}
    });
    // Load the bundled resource index before XAML requests its theme dictionary.
    resources=Microsoft::Windows::ApplicationModel::Resources::ResourceManager((ca::appDirectory()/L"ContextActions.pri").wstring());
    resources.MainResourceMap().GetValue(L"Files/Microsoft.UI.Xaml/Themes/themeresources.xbf");
    InitializeComponent();
}
void App::OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&) {
    auto main=winrt::make_self<MainWindow>();
    window=main.as<Microsoft::UI::Xaml::Window>();
    main->Configure(std::move(launchOptions),launchImmediate);
    window.Activate();
}
}
int ca::runUi(Options options,bool immediate) {
    winrt::init_apartment(winrt::apartment_type::single_threaded);
    struct Apartment { ~Apartment(){winrt::uninit_apartment();} } apartment;
    launchOptions=std::move(options);launchImmediate=immediate;
    winrt::Microsoft::UI::Xaml::Application::Start([](auto&&){winrt::make<winrt::ContextActions::implementation::App>();});
    return 0;
}
