# Сборка и проверка

Требуются Windows x64, Visual Studio 2022 C++ Build Tools (`v143`), Windows SDK **10.0.22621.0**, CMake **3.25+** и PowerShell 5.1/7. На машине пользователя готового приложения эти средства не нужны. MSVC runtime приложения статически связан; FFmpeg, ffprobe и qpdf с его DLL находятся в `engines/`.

```powershell
.\scripts\build-and-package.ps1
# Только компиляция и быстрый тест ядра:
.\scripts\build-and-package.ps1 -CompileOnly
# Полная сборка из уже скачанных проверяемых архивов без сети:
.\scripts\build-and-package.ps1 -Offline
```

Один сценарий используется локально и в `.github/workflows/build-windows.yml`. Загрузки только на этапе сборки, в `.build-tools/downloads/`, с зафиксированными URL/версиями и SHA-256 в `dependencies.json`. Кэш распаковывается заново из проверенного архива. Inno Setup берётся как переносимый `Tools.InnoSetup` из NuGet, не устанавливается в систему. Оригинальные инструкции и license Inno Setup сохраняются в build-кэше.

Пайплайн: CMake/MSVC Release → CTest → комплектование → короткий тест приложения → MakeAppx sparse identity → Inno Setup LZMA2 → отчёт и SHA-256. Ни один шаг не запускает установщик, shell-регистрацию, Office или Проводник. Тестовые файлы, настройки и временные файлы находятся внутри `build/`.

Выходы:

- `dist/ContextActions/ContextActions.exe` — приложение, можно запустить до установки.
- `dist/ContextActions-0.1.0-win-x64-setup.exe` и `.sha256` — полный установщик preview.
- `dist/reports/core-tests.xml`, `application-smoke.txt` — программные проверки.
- `dist/reports/size.json` — фактический размер EXE установщика и отдельно распакованный payload; размер сгенерированного Inno uninstaller и системного кэша MSIX в payload не включён.
- `dist/reports/components.json` — уникальные комплектные файлы, размеры, хеши и группы компонентов.
- `dist/reports/build-inputs.json` — конфигурация и обнаруженный toolchain.

Предыдущий установщик и отчёты инвалидируются до сборки. При превышении 300 МБ (300 000 000 байт) пайплайн падает с сохранением измерений; лимит и состав нельзя менять молча. Цель — до 100 МБ. Это размер самого EXE, не ZIP артефакта GitHub. Отчёт дополнительно показывает двоичные МиБ.

Зафиксирован Inno Setup 6.4.3 с лицензией, разрешающей бесплатное коммерческое использование. Sparse identity собирается с документированным ключом `/nv`, поскольку EXE/DLL находятся во внешней папке приложения; наличие этих файлов проверяется в stage. Это не проверка регистрации пакета в Windows.

## Ручная проверка контекстных меню позже

Установщик предлагает флажок **File Explorer context menus**, выключенный по умолчанию в preview. Можно установить только приложение, затем самостоятельно выполнить из установленной папки:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\packaging\register-shell.ps1 -Mode Install
# Удаление регистрации:
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\packaging\register-shell.ps1 -Mode Uninstall
```

Эти две команды **меняют Windows**: sparse MSIX даёт идентичность для современного меню, HKCU COM/ExplorerCommandHandler — для классического. Их не следует запускать как часть тестов приложения. Скрипт не перезапускает Проводник.

Preview использует неподписанную идентичность `ContextActions.Preview` с документированным unsigned OID. Windows-политика может потребовать повышение прав или подписанный пакет. Успех этой регистрации и реальная видимость обоих меню пока не подтверждены. Для публичной поставки нужны собственная подписанная идентичность без preview OID и отдельная проверка установки/обновления/удаления. Локальное создание установщика ничего не публикует.

Один набор выбранных файлов передаётся одним бинарным `.caj` в локальной папке `jobs/`; в командной строке только путь к заданию. Командные строки движков передаются через `CreateProcessW`, без CMD/PowerShell-интерполяции имён файлов. В пользовательском сценарии терминал не появляется.

Программный запуск приложения:

```powershell
.\dist\ContextActions\ContextActions.exe --headless --report .\build\result.txt --action h264 --output .\build\converted -- 'C:\Videos\пример.mp4'
```

Полный перечень CLI-параметров: `--action compress|h264|h265|telegram|image|pdf|rename|word|excel`, `--output`, `--quality`, `--width`, `--fps`, `--target-mb`, `--mute`, `--format`, `--first-frame`, `--pattern`, `--prefix`, `--suffix`, `--find`, `--replace`, `--start`; `--` завершает параметры. `--run` запускает выбранный пресет в GUI сразу.

Внешний путь настроек в тестах задаёт только переменная `CONTEXT_ACTIONS_DATA_DIR`. В обычной работе используется `%LOCALAPPDATA%\Context Actions`.

Источники решения shell-интеграции: [Microsoft IExplorerCommand и sparse package](https://learn.microsoft.com/en-us/windows/apps/desktop/modernize/integrate-packaged-app-with-file-explorer), [unsigned MSIX preview](https://learn.microsoft.com/en-us/windows/msix/package/unsigned-package).
