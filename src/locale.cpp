#include "platform.h"
#include <cstring>
#include <algorithm>
#include <mutex>

namespace ca {
namespace {
struct Text { const char* key; const wchar_t* values[5]; };
const Text texts[] = {
 {"tagline", {L"Everyday file actions, right where you need them.",L"Повседневные действия с файлами — всегда под рукой.",L"Alltägliche Dateiaktionen, direkt zur Hand.",L"Vos actions sur les fichiers, toujours à portée de main.",L"日常文件操作，触手可及。"}},
 {"filesTitle", {L"Files",L"Файлы",L"Dateien",L"Fichiers",L"文件"}},
 {"dropTitle", {L"Drop your files here",L"Перетащите сюда файлы",L"Dateien hier ablegen",L"Déposez vos fichiers ici",L"将文件拖到这里"}},
 {"dropHint", {L"Or add files to choose an action. Your originals are kept when converting.",L"Или добавьте файлы и выберите действие. При конвертации исходники сохраняются.",L"Oder Dateien hinzufügen und eine Aktion wählen. Originale bleiben beim Konvertieren erhalten.",L"Ou ajoutez des fichiers et choisissez une action. La conversion conserve les originaux.",L"也可添加文件后选择操作。转换时保留原始文件。"}},
 {"saveTo", {L"Save to",L"Сохранить в",L"Speichern unter",L"Enregistrer dans",L"保存到"}},
 {"sameFolder", {L"Same folder as source",L"Рядом с исходником",L"Ordner der Quelldatei",L"Dossier du fichier source",L"源文件所在文件夹"}},
 {"advanced", {L"Advanced options",L"Дополнительные параметры",L"Erweiterte Optionen",L"Options avancées",L"高级选项"}},
 {"errorDetails", {L"Error details",L"Подробности ошибки",L"Fehlerdetails",L"Détails de l’erreur",L"错误详情"}},
 {"category", {L"Category",L"Категория",L"Kategorie",L"Catégorie",L"类别"}},
 {"videoCategory", {L"Video",L"Видео",L"Video",L"Vidéo",L"视频"}},
 {"mediaCategory", {L"Media",L"Медиа",L"Medien",L"Médias",L"媒体"}},
 {"archiveCategory", {L"Archives",L"Архивы",L"Archive",L"Archives",L"归档文件"}},
 {"imageCategory", {L"Images",L"Изображения",L"Bilder",L"Images",L"图像"}},
 {"pdfCategory", {L"PDF",L"PDF",L"PDF",L"PDF",L"PDF"}},
 {"renameCategory", {L"Rename",L"Переименование",L"Umbenennen",L"Renommer",L"重命名"}},
 {"officeCategory", {L"Documents and spreadsheets",L"Документы и таблицы",L"Dokumente und Tabellen",L"Documents et feuilles de calcul",L"文档与电子表格"}},
 {"qualityPreset", {L"Quality",L"Качество",L"Qualität",L"Qualité",L"质量"}},
 {"highQuality", {L"High quality",L"Высокое качество",L"Hohe Qualität",L"Haute qualité",L"高质量"}},
 {"balanced", {L"Balanced",L"Баланс качества и размера",L"Ausgewogen",L"Équilibrée",L"均衡"}},
 {"smallerFile", {L"Smaller file",L"Меньший размер",L"Kleinere Datei",L"Fichier plus petit",L"更小文件"}},
 {"custom", {L"Custom",L"Вручную",L"Benutzerdefiniert",L"Personnalisée",L"自定义"}},
 {"settings", {L"Settings",L"Настройки",L"Einstellungen",L"Paramètres",L"设置"}},
 {"settingsHint", {L"Make Context Actions feel at home.",L"Настройте Context Actions для себя.",L"Context Actions nach Ihren Wünschen.",L"Adaptez Context Actions à vos préférences.",L"让 Context Actions 更符合您的习惯。"}},
 {"fullWorkspace", {L"Open full workspace",L"Открыть полное окно",L"Vollständigen Arbeitsbereich öffnen",L"Ouvrir l’espace de travail complet",L"打开完整工作区"}},
 {"back", {L"Back to files",L"Вернуться к файлам",L"Zurück zu den Dateien",L"Retour aux fichiers",L"返回文件"}},
 {"succeeded", {L"succeeded",L"успешно",L"erfolgreich",L"réussi(s)",L"成功"}},
 {"partialFailure", {L"Some files could not be processed. See their status in the file list; successful results remain available.",L"Часть файлов не удалось обработать. Причины указаны в списке; успешные результаты доступны.",L"Einige Dateien konnten nicht verarbeitet werden. Details stehen in der Dateiliste; erfolgreiche Ergebnisse bleiben verfügbar.",L"Certains fichiers n’ont pas pu être traités. Consultez leur état dans la liste ; les résultats réussis restent accessibles.",L"部分文件无法处理。请查看文件列表中的状态；成功的结果仍可使用。"}},
 {"more", {L"More Actions…",L"Другие действия…",L"Weitere Aktionen…",L"Autres actions…",L"更多操作…"}},
 {"chooseAction", {L"Choose an action",L"Выберите действие",L"Aktion auswählen",L"Choisir une action",L"选择操作"}},
 {"noApplicableActions", {L"No actions apply to these files.",L"Для этих файлов нет подходящих действий.",L"Für diese Dateien sind keine Aktionen verfügbar.",L"Aucune action ne s’applique à ces fichiers.",L"没有适用于这些文件的操作。"}},
 {"compress", {L"Compress video",L"Сжать видео",L"Video komprimieren",L"Compresser la vidéo",L"压缩视频"}},
 {"h264", {L"Convert to MP4 / H.264",L"Конвертировать в MP4 / H.264",L"In MP4 / H.264 umwandeln",L"Convertir en MP4 / H.264",L"转换为 MP4 / H.264"}},
 {"h265", {L"Compact copy / H.265",L"Компактная копия / H.265",L"Kompakte Kopie / H.265",L"Copie compacte / H.265",L"紧凑副本 / H.265"}},
 {"telegram", {L"Telegram animation…",L"Анимация для Telegram…",L"Telegram-Animation…",L"Animation Telegram…",L"Telegram 动画…"}},
 {"audio", {L"Extract or convert audio…",L"Извлечь или конвертировать звук…",L"Ton extrahieren oder konvertieren…",L"Extraire ou convertir l’audio…",L"提取或转换音频…"}},
 {"remux", {L"Change video container…",L"Сменить контейнер видео…",L"Videocontainer wechseln…",L"Changer le conteneur vidéo…",L"更换视频容器…"}},
 {"frame", {L"Save first video frame…",L"Сохранить первый кадр…",L"Erstes Videobild speichern…",L"Enregistrer la première image…",L"保存视频首帧…"}},
 {"media-info", {L"Media information…",L"Сведения о медиафайле…",L"Medieninformationen…",L"Informations sur le média…",L"媒体文件信息…"}},
 {"repack", {L"Repack to another format…",L"Перепаковать в другой формат…",L"In ein anderes Format umpacken…",L"Convertir vers un autre format d’archive…",L"重新打包为其他格式…"}},
 {"image", {L"Convert / resize images…",L"Конвертировать / уменьшить изображения…",L"Bilder konvertieren / skalieren…",L"Convertir / redimensionner les images…",L"转换 / 调整图像大小…"}},
 {"pdf", {L"Create / merge PDF…",L"Создать / объединить PDF…",L"PDF erstellen / zusammenführen…",L"Créer / fusionner des PDF…",L"创建 / 合并 PDF…"}},
 {"rename", {L"Rename files…",L"Переименовать файлы…",L"Dateien umbenennen…",L"Renommer les fichiers…",L"重命名文件…"}},
 {"word", {L"PDF with Word (original layout)",L"PDF через Word (исходное оформление)",L"PDF mit Word (Originallayout)",L"PDF avec Word (mise en page originale)",L"通过 Word 导出 PDF（原始布局）"}},
 {"excel", {L"PDF with Excel (original layout)",L"PDF через Excel (исходное оформление)",L"PDF mit Excel (Originallayout)",L"PDF avec Excel (mise en page originale)",L"通过 Excel 导出 PDF（原始布局）"}},
 {"documents", {L"Offline documents → PDF",L"Автономно: документы → PDF",L"Dokumente → PDF (offline)",L"Documents → PDF hors ligne",L"离线文档 → PDF"}},
 {"sheets", {L"Offline XLSX / XLS / ODS / CSV",L"Автономно: XLSX / XLS / ODS / CSV",L"XLSX / XLS / ODS / CSV (offline)",L"XLSX / XLS / ODS / CSV hors ligne",L"离线 XLSX / XLS / ODS / CSV"}},
 {"next", {L"Planned for the next stage; currently unavailable.",L"Следующий этап; пока недоступно.",L"Für die nächste Phase geplant; derzeit nicht verfügbar.",L"Prévu à l’étape suivante ; actuellement indisponible.",L"计划在下一阶段提供；当前不可用。"}},
 {"select", {L"Add files to choose an action.",L"Добавьте файлы для выбора действия.",L"Dateien für eine Aktion hinzufügen.",L"Ajoutez des fichiers pour choisir une action.",L"添加文件以选择操作。"}},
 {"unsupported", {L"This action does not support every selected file.",L"Действие поддерживает не все выбранные файлы.",L"Diese Aktion unterstützt nicht alle ausgewählten Dateien.",L"Cette action ne prend pas en charge tous les fichiers sélectionnés.",L"此操作不支持所有选中的文件。"}},
 {"needWord", {L"Microsoft Word is required. Install it, then choose Check again.",L"Требуется Microsoft Word. После установки нажмите «Проверить снова».",L"Microsoft Word erforderlich. Danach Erneut prüfen wählen.",L"Microsoft Word est requis. Installez-le, puis cliquez sur Revérifier.",L"需要 Microsoft Word。安装后请选择重新检查。"}},
 {"needExcel", {L"Microsoft Excel is required. Install it, then choose Check again.",L"Требуется Microsoft Excel. После установки нажмите «Проверить снова».",L"Microsoft Excel erforderlich. Danach Erneut prüfen wählen.",L"Microsoft Excel est requis. Installez-le, puis cliquez sur Revérifier.",L"需要 Microsoft Excel。安装后请选择重新检查。"}},
 {"officeHint", {L"Uses installed Office. Availability is checked on first use. Macros and link updates are disabled. Cancellation waits for the current Office call.",L"Использует установленный Office. Проверка при первом запуске. Макросы и обновление ссылок отключены. Отмена ждёт завершения текущего вызова Office.",L"Verwendet installiertes Office. Prüfung bei erster Nutzung. Makros und Linkupdates aus. Abbruch wartet auf den aktuellen Office-Aufruf.",L"Utilise Office installé. Vérification au premier usage. Macros et liens désactivés. L’annulation attend la fin de l’appel Office.",L"使用已安装的 Office，首次使用时检查。禁用宏和链接更新。取消需等待当前 Office 调用完成。"}},
 {"add", {L"Add files…",L"Добавить файлы…",L"Dateien hinzufügen…",L"Ajouter des fichiers…",L"添加文件…"}},
 {"remove", {L"Remove",L"Убрать",L"Entfernen",L"Retirer",L"移除"}},
 {"up", {L"Move up",L"Выше",L"Nach oben",L"Monter",L"上移"}},
 {"down", {L"Move down",L"Ниже",L"Nach unten",L"Descendre",L"下移"}},
 {"file", {L"File / page order",L"Файл / порядок страниц",L"Datei / Seitenreihenfolge",L"Fichier / ordre des pages",L"文件 / 页面顺序"}},
 {"status", {L"Status / new name",L"Статус / новое имя",L"Status / neuer Name",L"État / nouveau nom",L"状态 / 新名称"}},
 {"action", {L"Action",L"Действие",L"Aktion",L"Action",L"操作"}},
 {"output", {L"Output folder (empty = beside each source)",L"Папка результата (пусто = рядом с исходником)",L"Zielordner (leer = beim Original)",L"Dossier de sortie (vide = près de l’original)",L"输出文件夹（留空则与源文件相邻）"}},
 {"browse", {L"Browse…",L"Выбрать…",L"Durchsuchen…",L"Parcourir…",L"浏览…"}},
 {"run", {L"Run",L"Выполнить",L"Ausführen",L"Exécuter",L"执行"}},
 {"cancel", {L"Cancel",L"Отмена",L"Abbrechen",L"Annuler",L"取消"}},
 {"open", {L"Open result",L"Открыть результат",L"Ergebnis öffnen",L"Ouvrir le résultat",L"打开结果"}},
 {"reveal", {L"Show in folder",L"Показать в папке",L"Im Ordner anzeigen",L"Afficher dans le dossier",L"在文件夹中显示"}},
 {"retryOffice", {L"Check again (Office)",L"Проверить снова (Office)",L"Erneut prüfen (Office)",L"Revérifier (Office)",L"重新检查（Office）"}},
 {"undo", {L"Undo last rename",L"Отменить переименование",L"Umbenennung rückgängig",L"Annuler le renommage",L"撤销上次重命名"}},
 {"preview", {L"Preview names",L"Предпросмотр имён",L"Namen-Vorschau",L"Aperçu des noms",L"预览名称"}},
 {"ready", {L"Ready",L"Готово к запуску",L"Bereit",L"Prêt",L"就绪"}},
 {"working", {L"Processing…",L"Обработка…",L"Verarbeitung…",L"Traitement…",L"正在处理…"}},
 {"done", {L"Done",L"Готово",L"Fertig",L"Terminé",L"完成"}},
 {"failed", {L"Failed",L"Ошибка",L"Fehlgeschlagen",L"Échec",L"失败"}},
 {"cancelled", {L"Cancelled",L"Отменено",L"Abgebrochen",L"Annulé",L"已取消"}},
 {"cancelling", {L"Cancelling…",L"Отмена…",L"Wird abgebrochen…",L"Annulation…",L"正在取消…"}},
 {"invalid", {L"Invalid settings or file selection.",L"Некорректные параметры или набор файлов.",L"Ungültige Einstellungen oder Dateiauswahl.",L"Paramètres ou sélection de fichiers incorrects.",L"设置或文件选择无效。"}},
 {"wholeNumberRange", {L"Enter a whole number in this range:",L"Введите целое число в диапазоне:",L"Geben Sie eine ganze Zahl in diesem Bereich ein:",L"Saisissez un nombre entier dans cet intervalle :",L"请输入此范围内的整数："}},
 {"widthRange", {L"Enter 0 for the original size, or a whole number from 2 to 16384.",L"Введите 0 для исходного размера или целое число от 2 до 16384.",L"Geben Sie 0 für die Originalgröße oder eine ganze Zahl von 2 bis 16384 ein.",L"Saisissez 0 pour la taille originale, ou un nombre entier de 2 à 16384.",L"输入 0 保持原始尺寸，或输入 2 到 16384 之间的整数。"}},
 {"patternRequired", {L"Enter a name pattern, for example {name}_{n}.",L"Введите шаблон имени, например {name}_{n}.",L"Geben Sie ein Namensmuster ein, zum Beispiel {name}_{n}.",L"Saisissez un modèle de nom, par exemple {name}_{n}.",L"请输入名称模板，例如 {name}_{n}。"}},
 {"outputDirectory", {L"Choose a folder, or leave this empty to use each source folder. This path is a file.",L"Выберите папку или оставьте поле пустым для сохранения рядом с исходным файлом. Этот путь указывает на файл.",L"Wählen Sie einen Ordner, oder lassen Sie das Feld für den jeweiligen Quellordner leer. Dieser Pfad ist eine Datei.",L"Choisissez un dossier, ou laissez ce champ vide pour utiliser le dossier de chaque source. Ce chemin désigne un fichier.",L"请选择文件夹，或留空以使用各源文件所在的文件夹。此路径指向文件。"}},
 {"missing", {L"File is missing or is not a regular file: ",L"Файл отсутствует или не является обычным файлом: ",L"Datei fehlt oder ist keine reguläre Datei: ",L"Fichier absent ou non régulier : ",L"文件不存在或不是普通文件："}},
 {"engine", {L"Bundled engine is missing. Reinstall Context Actions: ",L"Нет комплектного движка. Переустановите Context Actions: ",L"Mitgelieferte Engine fehlt. Context Actions neu installieren: ",L"Moteur fourni absent. Réinstallez Context Actions : ",L"缺少内置引擎。请重新安装 Context Actions："}},
 {"systemEngine", {L"Required tool is missing: ",L"Не найдена необходимая программа: ",L"Benötigtes Programm fehlt: ",L"Outil requis introuvable : ",L"缺少所需工具："}},
 {"writerPdf", {L"PDF with LibreOffice Writer",L"PDF через LibreOffice Writer",L"PDF mit LibreOffice Writer",L"PDF avec LibreOffice Writer",L"通过 LibreOffice Writer 导出 PDF"}},
 {"calcPdf", {L"PDF with LibreOffice Calc",L"PDF через LibreOffice Calc",L"PDF mit LibreOffice Calc",L"PDF avec LibreOffice Calc",L"通过 LibreOffice Calc 导出 PDF"}},
 {"needLibreOffice", {L"Install LibreOffice Writer and Calc to export Office files to PDF.",L"Для экспорта файлов Office в PDF установите LibreOffice Writer и Calc.",L"Installieren Sie LibreOffice Writer und Calc für den PDF-Export.",L"Installez LibreOffice Writer et Calc pour exporter les fichiers Office en PDF.",L"请安装 LibreOffice Writer 和 Calc，以将 Office 文件导出为 PDF。"}},
 {"renameBusy", {L"Another rename or undo is running.",L"Выполняется другое переименование или отмена.",L"Eine andere Umbenennung oder Rückgängig-Aktion läuft.",L"Un autre renommage ou une annulation est en cours.",L"正在执行另一项重命名或撤销操作。"}},
 {"conflict", {L"Name conflict or invalid filename: ",L"Конфликт или недопустимое имя: ",L"Namenskonflikt oder ungültiger Dateiname: ",L"Conflit ou nom de fichier incorrect : ",L"名称冲突或无效文件名："}},
 {"sizeFailed", {L"Cannot reach the size limit within the quality floor. Reduce width/FPS or increase the limit.",L"Лимит размера недостижим в пределах качества. Уменьшите ширину/FPS или увеличьте лимит.",L"Größenlimit bei dieser Qualität nicht erreichbar. Breite/FPS reduzieren oder Limit erhöhen.",L"Limite impossible avec cette qualité. Réduisez largeur/FPS ou augmentez la limite.",L"无法在质量下限内达到大小限制。请降低宽度/帧率或提高限制。"}},
 {"quality", {L"Video CRF (0–51; lower = better)",L"CRF видео (0–51; ниже = лучше)",L"Video-CRF (0–51; kleiner = besser)",L"CRF vidéo (0–51 ; plus bas = mieux)",L"视频 CRF（0–51；越低越好）"}},
 {"imageQuality", {L"Compression (0–51; lower = better)",L"Сжатие (0–51; ниже = лучше)",L"Kompression (0–51; kleiner = besser)",L"Compression (0–51 ; plus bas = mieux)",L"压缩（0–51；越低越好）"}},
 {"width", {L"Max width (0 = original)",L"Макс. ширина (0 = исходная)",L"Max. Breite (0 = Original)",L"Largeur max. (0 = originale)",L"最大宽度（0 = 原始）"}},
 {"fps", {L"FPS (0 = original)",L"FPS (0 = исходный)",L"FPS (0 = Original)",L"FPS (0 = original)",L"帧率（0 = 原始）"}},
 {"target", {L"Size limit, MiB (0 = quality mode)",L"Лимит, МиБ (0 = режим качества)",L"Größenlimit, MiB (0 = Qualitätsmodus)",L"Limite, Mio (0 = mode qualité)",L"大小限制，MiB（0 = 质量模式）"}},
 {"mute", {L"Remove audio",L"Убрать звук",L"Ton entfernen",L"Retirer le son",L"移除音频"}},
 {"format", {L"Image format",L"Формат изображения",L"Bildformat",L"Format d’image",L"图像格式"}},
 {"archiveFormat", {L"Archive format",L"Формат архива",L"Archivformat",L"Format d’archive",L"归档格式"}},
 {"archiveFormatsExhausted", {L"Choose archives with a common target format different from each source format.",L"Выберите архивы, для которых есть общий формат, отличный от каждого исходного.",L"Wählen Sie Archive mit einem gemeinsamen Zielformat, das sich von allen Quellformaten unterscheidet.",L"Choisissez des archives ayant un format cible commun différent de tous les formats source.",L"请选择拥有共同目标格式的归档文件，且目标格式须与所有源格式不同。"}},
 {"firstFrame", {L"Use first frame / page only (GIF / TIFF / WebP)",L"Только первый кадр / страница (GIF / TIFF / WebP)",L"Nur erstes Bild / erste Seite (GIF / TIFF / WebP)",L"Première image / page seulement (GIF / TIFF / WebP)",L"仅使用第一帧 / 页（GIF / TIFF / WebP）"}},
 {"frameRequired", {L"Confirm first frame/page for GIF, TIFF or WebP. Animation and extra pages are discarded.",L"Подтвердите первый кадр/страницу для GIF, TIFF или WebP. Анимация и остальные страницы не сохраняются.",L"Erstes Bild/Seite für GIF, TIFF oder WebP bestätigen. Animation und weitere Seiten entfallen.",L"Confirmez la première image/page pour GIF, TIFF ou WebP. Animation et pages suivantes supprimées.",L"请确认 GIF、TIFF 或 WebP 仅使用第一帧/页。转换会丢弃动画和其他页面。"}},
 {"imageHint", {L"Still images. Metadata is removed; orientation is applied. JPEG/BMP and PDF flatten transparency onto white. Colour profiles are not preserved.",L"Статичные изображения. Метаданные удаляются; поворот применяется. JPEG/BMP и PDF: прозрачность заменяется белым. Цветовые профили не сохраняются.",L"Standbilder. Metadaten entfernt, Ausrichtung angewendet. JPEG/BMP und PDF: Transparenz wird weiß. Farbprofile entfallen.",L"Images fixes. Métadonnées retirées, orientation appliquée. Transparence sur blanc en JPEG/BMP et PDF. Profils couleur non conservés.",L"静态图像。移除元数据并应用方向。JPEG/BMP 和 PDF 将透明度合成到白色背景。不保留色彩配置。"}},
 {"pdfHint", {L"Order above is page order. Images fit A4 with 24 pt margins. Existing PDF pages keep their size. Forms, signatures and bookmarks are not merged.",L"Порядок сверху — порядок страниц. Изображения вписываются в A4 с полями 24 пт. PDF сохраняют размер страниц. Формы, подписи и закладки не объединяются.",L"Liste bestimmt Seitenfolge. Bilder auf A4 mit 24 pt Rand. PDF-Seitengrößen bleiben. Formulare, Signaturen und Lesezeichen entfallen.",L"L’ordre ci-dessus définit les pages. Images en A4, marges de 24 pt. Taille PDF conservée. Formulaires, signatures et signets non fusionnés.",L"上方顺序即页面顺序。图像适配 A4，边距 24 点。PDF 页面保留尺寸。不合并表单、签名和书签。"}},
 {"videoHint", {L"MP4 output. Software H.264/H.265, first video/audio tracks; subtitles and extra tracks are removed. Telegram: H.264, silent, default 720 px / 30 FPS. Size limit is checked before saving.",L"Результат MP4. Программные H.264/H.265, первые видео/аудиодорожки; субтитры и лишние дорожки удаляются. Telegram: H.264 без звука, 720 px / 30 FPS по умолчанию. Лимит размера проверяется до сохранения.",L"MP4-Ausgabe. Software-H.264/H.265, erste Video-/Tonspur; Untertitel und weitere Spuren entfallen. Telegram: H.264, stumm, 720 px / 30 FPS. Größenprüfung vor dem Speichern.",L"Sortie MP4. H.264/H.265 logiciel, premières pistes vidéo/audio ; sous-titres et autres pistes retirés. Telegram : H.264 muet, 720 px / 30 FPS. Taille vérifiée avant sauvegarde.",L"输出 MP4。软件 H.264/H.265，首个视频/音频轨道；移除字幕和其他轨道。Telegram 默认静音 H.264、720 像素/30 帧。保存前检查大小。"}},
 {"audioHint", {L"Save the first audio track in the selected format.",L"Сохраняет первую аудиодорожку в выбранном формате.",L"Speichert die erste Tonspur im gewählten Format.",L"Enregistre la première piste audio dans le format choisi.",L"以所选格式保存第一条音轨。"}},
 {"remuxHint", {L"Copy all tracks without re-encoding. The selected container must support them.",L"Копирует все дорожки без перекодирования. Выбранный контейнер должен поддерживать их.",L"Kopiert alle Spuren ohne Neukodierung. Der gewählte Container muss sie unterstützen.",L"Copie toutes les pistes sans réencodage. Le conteneur choisi doit les prendre en charge.",L"直接复制所有轨道。所选容器必须支持这些轨道。"}},
 {"frameHint", {L"Save the first video frame as a PNG image.",L"Сохраняет первый кадр видео в PNG.",L"Speichert das erste Videobild als PNG.",L"Enregistre la première image de la vidéo en PNG.",L"将视频的第一帧保存为 PNG 图像。"}},
 {"mediaInfoHint", {L"Save the format and track details as a text report. Open it from the results.",L"Сохраняет формат и параметры дорожек в текстовый отчёт. Его можно открыть из результатов.",L"Speichert Format und Spurdaten als Textbericht. Der Bericht lässt sich über die Ergebnisse öffnen.",L"Enregistre le format et les détails des pistes dans un rapport texte. Ouvrez-le depuis les résultats.",L"将格式和轨道详情保存为文本报告，可从结果中打开。"}},
 {"repackHint", {L"Save each archive as a separate copy in the selected format.",L"Сохраняет каждый архив отдельной копией в выбранном формате.",L"Speichert jedes Archiv als separate Kopie im gewählten Format.",L"Enregistre chaque archive dans une copie distincte au format choisi.",L"将每个归档文件分别保存为所选格式的副本。"}},
 {"pattern", {L"Name pattern: {name}, {n}; extension kept",L"Шаблон: {name}, {n}; расширение сохраняется",L"Namensmuster: {name}, {n}; Endung bleibt",L"Modèle : {name}, {n} ; extension conservée",L"名称模板：{name}、{n}；保留扩展名"}},
 {"prefix", {L"Prefix",L"Префикс",L"Präfix",L"Préfixe",L"前缀"}},
 {"suffix", {L"Suffix",L"Суффикс",L"Suffix",L"Suffixe",L"后缀"}},
 {"find", {L"Replace text",L"Заменить текст",L"Text ersetzen",L"Texte à remplacer",L"要替换的文本"}},
 {"replace", {L"With",L"На",L"Durch",L"Par",L"替换为"}},
 {"start", {L"Start number",L"Начальный номер",L"Startnummer",L"Numéro de départ",L"起始编号"}},
 {"renameHint", {L"Preview is required before renaming. Conflicts stop the whole batch. Undo is available until the next rename, if files have not changed.",L"Перед переименованием нужен предпросмотр. Конфликт останавливает весь пакет. Отмена доступна до следующего переименования, если файлы не изменились.",L"Vorschau vor Umbenennen erforderlich. Konflikte stoppen den Stapel. Rückgängig bis zur nächsten Umbenennung, sofern Dateien unverändert.",L"Aperçu requis avant renommage. Les conflits arrêtent le lot. Annulation jusqu’au prochain renommage si les fichiers restent inchangés.",L"重命名前需要预览。冲突将停止整个批次。文件未改变时，可在下次重命名前撤销。"}},
 {"previewRequired", {L"Preview the current names first.",L"Сначала обновите предпросмотр имён.",L"Zuerst aktuelle Namen in der Vorschau prüfen.",L"Actualisez d’abord l’aperçu des noms.",L"请先预览当前名称。"}},
 {"language", {L"Language",L"Язык",L"Sprache",L"Langue",L"语言"}},
 {"theme", {L"Theme",L"Тема",L"Design",L"Thème",L"主题"}},
 {"system", {L"System",L"Системная",L"System",L"Système",L"系统"}},
 {"light", {L"Light",L"Светлая",L"Hell",L"Clair",L"浅色"}},
 {"dark", {L"Dark",L"Тёмная",L"Dunkel",L"Sombre",L"深色"}},
};
std::mutex localeMutex;
std::wstring selectedLanguage;
}

std::wstring language() {
    std::lock_guard lock(localeMutex);
    if (selectedLanguage.empty()) selectedLanguage = readLanguageSetting();
    return selectedLanguage;
}
void setLanguage(const std::wstring& value) {
    std::lock_guard lock(localeMutex);
    ensurePrivateDirectory(dataDirectory());
    writeLanguageSetting(value);
    selectedLanguage = value;
}
std::wstring tr(const char* key) {
    const auto lang = language();
    int index = lang.starts_with(L"ru") ? 1 : lang.starts_with(L"de") ? 2 : lang.starts_with(L"fr") ? 3 : lang.starts_with(L"zh") ? 4 : 0;
    for (const auto& text : texts) if (std::strcmp(key, text.key) == 0) return text.values[index];
    return wide(key);
}
}
