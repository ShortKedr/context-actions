# Сборка и проверка

Общее ядро и CLI собираются на каждой платформе. Windows использует WinUI 3 и расширение File Explorer, macOS использует AppKit и Finder, Linux использует GTK 4 и freedesktop desktop entry.

## Windows 11 x64

Требуются Windows 11 x64, Visual Studio 2022 C++ Build Tools (`v143`), Windows SDK **10.0.22621.0**, CMake **3.25+** и PowerShell 5.1/7. Для native XAML нужны компоненты **C++ WinUI app development build tools** и **Universal Windows Platform build tools**: `Microsoft.VisualStudio.ComponentGroup.UWP.VC.BuildTools` и `Microsoft.VisualStudio.ComponentGroup.UWP.BuildTools`. Скрипты проверяют их наличие, но не устанавливают системные компоненты.

На машине пользователя готового приложения средства сборки не нужны. WinUI 3 собран с `WindowsAppSDKSelfContained=true`: DLL и PRI Windows App SDK находятся рядом с EXE. Приложение использует статический MSVC runtime и системный UCRT Windows 11; FFmpeg, ffprobe и qpdf с его DLL находятся в `engines/`. Переносить нужно всю папку, а не только EXE. Автономный запуск на чистой Windows ещё требует проверки.

```powershell
.\scripts\build-and-package.ps1
# Только компиляция и быстрый тест ядра:
.\scripts\build-and-package.ps1 -CompileOnly
# Полная сборка из уже скачанных проверяемых архивов без сети:
.\scripts\build-and-package.ps1 -Offline
# Сборка и установщик без запуска любых тестов:
.\scripts\build-and-package.ps1 -Offline -SkipTests
# Только WinUI и общее ядро, без запуска приложения:
.\scripts\build-ui.ps1 -Offline
```

Один сценарий используется локально и в `.github/workflows/build-windows.yml`. Архивы движков и Inno Setup загружаются в `.build-tools/downloads/`, с зафиксированными URL/версиями и SHA-256 в `dependencies.json`; распаковка повторяется из проверенного архива. Девять Microsoft NuGet-пакетов для WinUI зафиксированы в `src/ui/packages.config` и восстанавливаются с nuget.org в `.build-tools/nuget/`. NuGet CLI 6.14.0 также проверяется по SHA-256. Inno Setup берётся как переносимый `Tools.InnoSetup`, не устанавливается в систему.

Пайплайн: CMake/MSVC Release (ядро, shell-DLL, тестовый EXE) → MSBuild native XAML → CTest → комплектование WinUI и движков → короткий headless-тест приложения → MakeAppx sparse identity → Inno Setup LZMA2 → отчёт и SHA-256. `-SkipTests` пропускает CTest и headless-тест; `test-status.json` явно фиксирует отсутствие проверки. UI-тест не запускается автоматически. Ни один шаг не запускает установщик, shell-регистрацию, Office или Проводник. Тестовые файлы, настройки и временные файлы находятся внутри `build/`.

Для отдельной проверки интерфейса запустите `./scripts/ui-smoke-test.ps1 -AppPath ./dist/ContextActions/ContextActions.exe` в интерактивном сеансе Windows. Скрипт открывает приложение и проверяет выбор файлов, переименование и отмену, переходы между режимами окна, валидацию и диагностику ошибок.

Исходники старого UI в `src/ui.cpp` временно сохранены для сравнения до проверки паритета; они больше не компилируются и не входят в поставку.

Выходы:

- `dist/ContextActions/ContextActions.exe` — приложение, можно запустить до установки.
- `dist/ContextActions-0.1.0-win-x64-setup.exe` и `.sha256` — полный установщик preview.
- `dist/reports/core-tests.xml`, `application-smoke.txt` — программные проверки.
- `dist/reports/test-status.json` — выполненные и пропущенные проверки текущей сборки; отсутствие запуска не считается успехом.
- `dist/reports/size.json` — фактический размер EXE установщика и отдельно распакованный payload; размер сгенерированного Inno uninstaller и системного кэша MSIX в payload не включён.
- `dist/reports/components.json` — уникальные комплектные файлы, размеры, хеши и группы компонентов.
- `dist/reports/build-inputs.json` — конфигурация и обнаруженный toolchain.
- `licenses/windows-app-sdk/` внутри поставки — зафиксированные версии Microsoft-пакетов и их комплектные license/NOTICE.

Предыдущий установщик и отчёты инвалидируются до сборки. При превышении 300 МБ (300 000 000 байт) пайплайн падает с сохранением измерений; лимит и состав нельзя менять молча. Цель — до 100 МБ. Это размер самого EXE, не ZIP артефакта GitHub. Отчёт дополнительно показывает двоичные МиБ.

Зафиксирован Inno Setup 6.4.3 с лицензией, разрешающей бесплатное коммерческое использование. Sparse identity собирается с документированным ключом `/nv`, поскольку EXE/DLL находятся во внешней папке приложения; наличие этих файлов проверяется в stage. Это не проверка регистрации пакета в Windows.

## Регистрация и проверка контекстных меню

Установщик предлагает флажок **File Explorer context menus**, выключенный по умолчанию в preview. Эта локальная preview-регистрация требует уже включённого Developer Mode. Скрипт не включает его, не устанавливает сертификаты и не повышает права. Можно установить только приложение, затем выполнить из установленной папки:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\packaging\register-shell.ps1 -Mode Install -Development
# Удаление регистрации:
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\packaging\register-shell.ps1 -Mode Uninstall
```

Эти две команды **меняют Windows**: loose manifest с внешней папкой приложения регистрирует package identity для современного меню, HKCU COM/ExplorerCommandHandler — для классического. Их не следует запускать как часть автоматической сборки или headless-тестов. Скрипт не перезапускает Проводник. Зарегистрированную папку нужно сохранять до удаления регистрации; для разработки используйте отдельную копию вне пересоздаваемого `dist/`.

В проверке 2026-09-09 прежний `-AllowUnsigned` завершился ошибкой `0x80073D2B`: unsigned package с executable activations не устанавливается в обычном пользовательском контексте. Регистрация через `-Development` успешно работает при уже включённом Developer Mode. Современное меню проверено в Проводнике: фильтрация команд, H.264 с явным Run, смешанный выбор и More Actions. Классическое меню пока не показывает Context Actions; наличие COM-регистрации не считается прохождением этого теста.

Без `-Development` скрипт ожидает доверенный подписанный `ContextActions.msix`. Сборка создаёт неподписанный артефакт с обычным Publisher `CN=Context Actions`; подпись автоматически не добавляется. Для публичной поставки нужны собственная доверенная подписанная идентичность и отдельная проверка установки/обновления/удаления. Локальное создание установщика ничего не публикует.

Один набор выбранных файлов передаётся одним бинарным `.caj` в локальной папке `jobs/`; в командной строке только путь к заданию. Командные строки движков передаются через `CreateProcessW`, без CMD/PowerShell-интерполяции имён файлов. В пользовательском сценарии терминал не появляется.

Программный запуск приложения:

```powershell
.\dist\ContextActions\ContextActions.exe --headless --report .\build\result.txt --action h264 --output .\build\converted -- 'C:\Videos\пример.mp4'
```

Полный перечень CLI-параметров: `--action compress|h264|h265|telegram|image|pdf|rename|word|excel`, `--output`, `--quality`, `--width`, `--fps`, `--target-mb`, `--mute`, `--format`, `--first-frame`, `--pattern`, `--prefix`, `--suffix`, `--find`, `--replace`, `--start`; `--` завершает параметры. Явный `--run` запускает выбранный пресет в GUI сразу; переименование всё равно требует предпросмотра. Контекстное меню больше не добавляет этот флаг: сначала показываются настройки и кнопка Run.

Внешний путь настроек в тестах задаёт только переменная `CONTEXT_ACTIONS_DATA_DIR`. В обычной работе используется `%LOCALAPPDATA%\Context Actions`.

Источники решения shell-интеграции: [Microsoft IExplorerCommand и sparse package](https://learn.microsoft.com/en-us/windows/apps/desktop/modernize/integrate-packaged-app-with-file-explorer), [unsigned MSIX preview](https://learn.microsoft.com/en-us/windows/msix/package/unsigned-package).

## macOS Universal

Нужны Xcode Command Line Tools, CMake 3.25+ и Ninja. Для обработки медиа установите `brew install ffmpeg-full qpdf`. Приложение находит keg-only `ffmpeg-full` и qpdf в стандартных каталогах Homebrew при запуске из Finder. Для экспорта Office в PDF установите LibreOffice Writer и Calc.

```bash
cmake -S . -B build/macos -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES='arm64;x86_64' -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0
cmake --build build/macos
ctest --test-dir build/macos --output-on-failure
export PATH="$(brew --prefix ffmpeg-full)/bin:$PATH"
bash scripts/smoke-test-posix.sh build/macos/ContextActions.app/Contents/MacOS/ContextActions build/macos
bash packaging/macos/package.sh build/macos dist/macos
```

Приложение `build/macos/ContextActions.app` содержит срезы для Intel и Apple Silicon. Упаковка создаёт ZIP приложения и PKG для установки в `/Applications` в `dist/macos`. У приложения ad-hoc подпись, PKG без Developer ID и нотариального заверения. FFmpeg, qpdf и LibreOffice устанавливаются отдельно. Finder может открывать файлы через **Open With**, а CLI доступен через `ContextActions.app/Contents/MacOS/ContextActions`. Настройки и журнал переименования хранятся в `~/Library/Application Support/Context Actions`. [Документация Apple о Universal Binary](https://developer.apple.com/documentation/apple-silicon/building-a-universal-macos-binary), [Homebrew ffmpeg-full](https://formulae.brew.sh/formula/ffmpeg-full).

## Linux

GTK 4.6+ используется на всех дистрибутивах Linux. Ниже приведена сборка на Ubuntu 24.04, которая служит базовой системой CI. На других дистрибутивах установите эквивалентные пакеты GTK 4, компилятора C++20 и инструментов сборки. FFmpeg должен содержать энкодеры `libx264`, `libx265` и `libwebp`.

```bash
sudo apt install g++ cmake ninja-build pkg-config libgtk-4-dev ffmpeg qpdf dpkg-dev rpm cpio
cmake -S . -B build/linux -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/linux
ctest --test-dir build/linux --output-on-failure
bash scripts/smoke-test-posix.sh build/linux/src/linux/context-actions build/linux
cpack --config build/linux/CPackConfig.cmake -B dist/linux -G 'TGZ;DEB;RPM'
```

DEB устанавливается через `apt install ./<имя-пакета>.deb`, RPM через `dnf install ./<имя-пакета>.rpm`. Для Fedora может понадобиться RPM Fusion Free с полным FFmpeg. В архиве приложение находится в `usr/bin/context-actions`, ему нужны совместимые системные библиотеки GTK и инструменты FFmpeg и qpdf. Для экспорта Office в PDF нужны пакеты LibreOffice Writer и Calc. После установки ярлык **Context Actions** появляется в меню приложений и в **Open With** для зарегистрированных типов файлов. Настройки хранятся в `$XDG_DATA_HOME/context-actions` или `~/.local/share/context-actions`. [Зависимости GTK по дистрибутивам](https://www.gtk.org/docs/installations/linux).

## GitHub Actions

Три workflow собирают приложение и установщик при push, pull request и ручном запуске. Артефакты и SHA-256 доступны в результатах workflow. Windows собирается для x64, macOS получает один универсальный пакет для обеих архитектур, Linux собирается отдельно для amd64 и arm64.
