#include "pch.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"
#include <microsoft.ui.xaml.window.h>
#include <fstream>

namespace {
ca::Options launchOptions;
bool launchImmediate=false;
bool launchChooser=false;

void positionChooser(winrt::Microsoft::UI::Xaml::Window const& window, int height) {
    HWND handle{};
    winrt::check_hresult(window.as<IWindowNative>()->get_WindowHandle(&handle));
    auto appWindow = window.AppWindow();
    POINT pointer{};
    GetCursorPos(&pointer);
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    if (!GetMonitorInfoW(MonitorFromPoint(pointer, MONITOR_DEFAULTTONEAREST), &monitor)) {
        const auto dpi = GetDpiForWindow(handle);
        appWindow.Resize({MulDiv(440, dpi, 96), MulDiv(height, dpi, 96)});
        return;
    }
    const auto& area = monitor.rcWork;
    // Give the hidden window a footprint entirely on the target display before reading its DPI.
    appWindow.MoveAndResize({area.left, area.top, 100, 100});
    const auto dpi = GetDpiForWindow(handle);
    const LONG width = std::min<LONG>(MulDiv(440, dpi, 96), area.right - area.left);
    const LONG pixelsHigh = std::min<LONG>(MulDiv(height, dpi, 96), area.bottom - area.top);
    const LONG gap = MulDiv(10, dpi, 96);
    appWindow.MoveAndResize({
        std::clamp(pointer.x + gap, area.left, area.right - width),
        std::clamp(pointer.y + gap, area.top, area.bottom - pixelsHigh),
        width,
        pixelsHigh,
    });
}
}
namespace winrt::ContextActions::implementation {
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Automation;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Input;

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
    if (launchChooser) ShowActionChooser();
    else ShowWorkspace(std::move(launchOptions), launchImmediate);
}

void App::ShowWorkspace(ca::Options options, bool immediate) {
    auto main=winrt::make_self<MainWindow>();
    main->Configure(std::move(options), immediate);
    window=main.as<Microsoft::UI::Xaml::Window>();
    window.Activate();
}

void App::ShowActionChooser() {
    const auto choices = ca::actionChoices(launchOptions.files);
    window = Window();
    window.Title(L"Context Actions");
    auto presenter = Microsoft::UI::Windowing::OverlappedPresenter::CreateForToolWindow();
    presenter.IsResizable(false);
    presenter.IsMaximizable(false);
    presenter.IsMinimizable(false);
    window.AppWindow().SetPresenter(presenter);

    Grid root;
    root.Padding({16, 12, 16, 12});
    root.RowSpacing(12);
    root.XYFocusKeyboardNavigation(XYFocusKeyboardNavigationMode::Enabled);
    AutomationProperties::SetAutomationId(root, L"ActionChooser");
    for (auto unit : {GridUnitType::Auto, GridUnitType::Star, GridUnitType::Auto}) {
        RowDefinition row;
        row.Height({1, unit});
        root.RowDefinitions().Append(row);
    }
    TextBlock title;
    title.Text(ca::tr("chooseAction"));
    title.FontSize(20);
    title.FontWeight(Windows::UI::Text::FontWeight{600});
    title.TextWrapping(TextWrapping::Wrap);
    AutomationProperties::SetHeadingLevel(title, Peers::AutomationHeadingLevel::Level1);
    root.Children().Append(title);

    Grid tiles;
    tiles.ColumnSpacing(8);
    tiles.RowSpacing(8);
    for (int i = 0; i < 2; ++i) {
        ColumnDefinition column;
        column.Width({1, GridUnitType::Star});
        tiles.ColumnDefinitions().Append(column);
    }
    Button first{nullptr};
    for (std::size_t i = 0; i < choices.size(); ++i) {
        const auto& choice = choices[i];
        if (i % 2 == 0) {
            RowDefinition row;
            row.Height({1, GridUnitType::Auto});
            tiles.RowDefinitions().Append(row);
        }
        Button tile;
        tile.HorizontalAlignment(HorizontalAlignment::Stretch);
        tile.VerticalAlignment(VerticalAlignment::Stretch);
        tile.HorizontalContentAlignment(HorizontalAlignment::Left);
        tile.MinHeight(40);
        tile.Padding({10, 6, 10, 6});
        AutomationProperties::SetAutomationId(tile, L"ChooseAction-" + ca::actionId(choice.action));
        AutomationProperties::SetName(tile, choice.label);
        StackPanel text;
        TextBlock name;
        name.Text(choice.label);
        name.TextWrapping(TextWrapping::Wrap);
        text.Children().Append(name);
        if (!choice.unavailable.empty()) {
            TextBlock reason;
            reason.Text(choice.unavailable);
            reason.TextWrapping(TextWrapping::Wrap);
            reason.FontSize(12);
            text.Children().Append(reason);
            AutomationProperties::SetHelpText(tile, choice.unavailable);
            tile.IsEnabled(false);
        } else if (!first) {
            first = tile;
        }
        tile.Content(text);
        tile.Click([this, action = choice.action](auto&&, auto&&) {
            auto chooser = window;
            launchOptions.action = action;
            ShowWorkspace(std::move(launchOptions), false);
            chooser.Close();
        });
        Grid::SetRow(tile, static_cast<int>(i / 2));
        Grid::SetColumn(tile, static_cast<int>(i % 2));
        tiles.Children().Append(tile);
    }
    ScrollViewer scroll;
    scroll.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
    scroll.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);
    if (choices.empty()) {
        TextBlock empty;
        empty.Text(ca::tr("noApplicableActions"));
        empty.TextWrapping(TextWrapping::Wrap);
        scroll.Content(empty);
    } else {
        scroll.Content(tiles);
    }
    Grid::SetRow(scroll, 1);
    root.Children().Append(scroll);

    Button cancel;
    cancel.HorizontalAlignment(HorizontalAlignment::Right);
    cancel.Content(box_value(ca::tr("cancel")));
    AutomationProperties::SetAutomationId(cancel, L"CancelChooser");
    cancel.Click([this](auto&&, auto&&) { window.Close(); });
    KeyboardAccelerator escape;
    escape.Key(Windows::System::VirtualKey::Escape);
    escape.Invoked([this](auto&&, KeyboardAcceleratorInvokedEventArgs const& args) {
        args.Handled(true);
        window.Close();
    });
    root.KeyboardAccelerators().Append(escape);
    Grid::SetRow(cancel, 2);
    root.Children().Append(cancel);
    const auto focus = first ? first : cancel;
    root.Loaded([weak = make_weak(focus)](auto&&, auto&&) {
        if (auto control = weak.get()) control.Focus(FocusState::Programmatic);
    });

    window.Content(root);
    const auto rows = (static_cast<int>(choices.size()) + 1) / 2;
    const bool hasUnavailable = std::any_of(choices.begin(), choices.end(), [](auto const& choice) {
        return !choice.unavailable.empty();
    });
    positionChooser(window, std::clamp(132 + rows * (hasUnavailable ? 64 : 48), 188, 460));
    window.Activate();
}
}
int ca::runUi(Options options,bool immediate,bool chooseAction) {
    winrt::init_apartment(winrt::apartment_type::single_threaded);
    struct Apartment { ~Apartment(){winrt::uninit_apartment();} } apartment;
    launchOptions=std::move(options);launchImmediate=immediate;launchChooser=chooseAction;
    winrt::Microsoft::UI::Xaml::Application::Start([](auto&&){winrt::make<winrt::ContextActions::implementation::App>();});
    return 0;
}
