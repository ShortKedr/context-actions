# Комплектные компоненты 0.1

| Компонент | Версия / состав | Использование |
| --- | --- | --- |
| Context Actions | C++20, Win32, статический MSVC runtime | UI, CLI, shell-DLL, локализация, очередь и переименование |
| FFmpeg Gyan essentials x64 | 8.0.1; ffmpeg.exe и ffprobe.exe; программные libx264/libx265/libwebp и транзитивные библиотеки статической сборки | Видео и изображения; GPLv3-сборка, исходные LICENSE/README и notices включены в licenses/ffmpeg |
| qpdf MinGW x64 | 12.4.1; qpdf.exe, qpdf30.dll, libgcc_s_seh-1.dll, libstdc++-6.dll, libwinpthread-1.dll | PDF-проверки и объединение; Apache-2.0 и отдельные лицензии транзитивных runtime; каталог share включён в licenses/qpdf |
| Windows API / WIC | Системные компоненты Windows 11 | Нативные элементы, COM, чтение размеров JPEG; дополнительные WIC-codec extensions не нужны |
| Inno Setup | 6.4.3, переносимый NuGet Tools.InnoSetup | Только инструмент сборки, не runtime продукта. Зафиксированная лицензия в архиве разрешает в том числе коммерческое использование. [Исходный релиз](https://github.com/jrsoftware/issrc/releases/tag/is-6_4_3) |
| Windows SDK MakeAppx | 10.0.22621.0 | Только упаковка sparse identity, не установка |

Точные URL и SHA-256 архивов: `dependencies.json`. Распакованные размеры уникальных файлов и хеши генерируются в `dist/reports/components.json`. Реальная конфигурация FFmpeg доступна командой `engines/ffmpeg.exe -buildconf`; эти статические EXE содержат больше кодеков, чем заявленная матрица приложения.

Это локальная preview-поставка. Перед публичным распространением нужно подготовить соответствующие исходники и необходимые материалы конкретных GPL-компонентов и транзитивных библиотек: наличие LICENSE/ссылки само по себе не выполняет все обязательства по распространению бинарников. Приложение взаимодействует с FFmpeg/qpdf отдельными процессами. Публикация и назначение лицензии собственному коду не выполнялись.

Источники: [конкретный FFmpeg 8.0.1](https://github.com/GyanD/codexffmpeg/releases/tag/8.0.1), [состав сборок Gyan](https://www.gyan.dev/ffmpeg/builds/), [FFmpeg legal](https://ffmpeg.org/legal.html), [qpdf 12.4.1](https://github.com/qpdf/qpdf/releases/tag/v12.4.1), [операции qpdf со страницами](https://qpdf.readthedocs.io/en/stable/cli.html#page-selection).
