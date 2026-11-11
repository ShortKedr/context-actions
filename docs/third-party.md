# Комплектные компоненты 0.1

| Компонент | Версия / состав | Использование |
| --- | --- | --- |
| Context Actions | C++20, WinUI 3 + C++/WinRT, статический MSVC runtime и системный UCRT | UI, CLI, shell-DLL, локализация, очередь и переименование |
| Windows App SDK | WinUI package 2.3.6, Runtime 2.4.0, Foundation 2.3.9, InteractiveExperiences 2.1.3, Base 2.0.4 | Нативный XAML/WinUI 3 и его транзитивные компоненты, self-contained DLL/PRI рядом с EXE; license/NOTICE в licenses/windows-app-sdk |
| Microsoft build dependencies | CppWinRT 3.0.260818.1, SDK.BuildTools 10.0.26100.4654, SDK.BuildTools.MSIX 1.7.251221100, WebView2 1.0.3719.77 | Генерация и сборка; WebView2 — транзитивная зависимость WinUI, приложение не использует браузерный интерфейс |
| FFmpeg Gyan full x64 | 9.0.2; ffmpeg.exe и ffprobe.exe; декодер AV1 libdav1d, программные libx264/libx265/libwebp и транзитивные библиотеки статической сборки | Видео и изображения; GPLv3-сборка, LICENSE/README из архива сохраняются в licenses/ffmpeg при установке |
| qpdf MinGW x64 | 12.4.2; qpdf.exe, qpdf30.dll, libgcc_s_seh-1.dll, libstdc++-6.dll, libwinpthread-1.dll | PDF-проверки и объединение; Apache-2.0 и отдельные лицензии транзитивных runtime; каталог share сохраняется в licenses/qpdf при установке |
| Windows API | Системные компоненты Windows 11 | Нативные элементы и COM. Размеры JPEG читает общий движок через ffprobe |
| Inno Setup | 6.4.3, переносимый NuGet Tools.InnoSetup | Только инструмент сборки, не runtime продукта. Зафиксированная лицензия в архиве разрешает в том числе коммерческое использование. [Исходный релиз](https://github.com/jrsoftware/issrc/releases/tag/is-6_4_3) |
| Windows SDK MakeAppx | 10.0.22621.0 | Только упаковка sparse identity, не установка |

Точные URL и SHA-256 архивов: `dependencies.json`; Microsoft NuGet-пакеты: `src/ui/packages.config`. Распакованные размеры уникальных файлов и хеши генерируются в `dist/reports/components.json`. Реальная конфигурация FFmpeg доступна командой `engines/ffmpeg.exe -buildconf`; эти статические EXE содержат больше кодеков, чем заявленная матрица приложения.

Код Context Actions распространяется по MIT. Публичный Windows ZIP не включает FFmpeg и qpdf, а установщик релиза скачивает их напрямую из закреплённых релизов поставщиков. Локальная сборка без `-Release` включает движки для автономного тестирования. При распространении такой сборки действуют лицензии включённых компонентов, в том числе требования GPL к предоставлению соответствующего исходного кода FFmpeg и связанных библиотек. Приложение взаимодействует с движками отдельными процессами.

[Сборка FFmpeg 9.0.2](https://github.com/GyanD/codexffmpeg/releases/tag/9.0.2), [ревизия исходного кода FFmpeg](https://github.com/FFmpeg/FFmpeg/commit/946fcce07b), [состав сборок Gyan](https://www.gyan.dev/ffmpeg/builds/), [лицензирование FFmpeg](https://ffmpeg.org/legal.html), [релиз qpdf 12.4.2](https://github.com/qpdf/qpdf/releases/tag/v12.4.2).

В macOS интерфейс использует системный AppKit. В Linux интерфейс динамически связан с системной GTK 4. Обе платформы запускают установленный в системе FFmpeg, ffprobe и qpdf отдельными процессами. Для экспорта Office в PDF они используют отдельно установленный LibreOffice Writer и Calc.
