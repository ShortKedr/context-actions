# Context Actions 0.1 supported formats and file operations

Context Actions supports Windows 11 x64, macOS 11+ on Intel and Apple Silicon, and Linux x86-64 and ARM64 with GTK 4.6+. See the platform build and dependency details. [Build instructions](build.md)

## Format conversions

| Action | Input formats | Output format | Behavior and limits |
| --- | --- | --- | --- |
| Compress video or convert to H.264 | MP4, MKV, MOV, AVI, WebM, M4V, WMV, MTS, M2TS | MP4 with H.264 | Uses the software encoder libx264. The first video stream and first audio stream are used. Audio is encoded as AAC at 128 kb/s or omitted. Extra tracks and subtitles are dropped. |
| Convert to H.265 | MP4, MKV, MOV, AVI, WebM, M4V, WMV, MTS, M2TS | MP4 with H.265 | Uses the software encoder libx265 and the same stream and audio rules as H.264. |
| Telegram animation preset | MP4, MKV, MOV, AVI, WebM, M4V, WMV, MTS, M2TS | Silent MP4 with H.264 | Defaults to 720 px maximum width and 30 fps. Playback as an animation in Telegram clients still needs verification. |
| Convert or resize images | JPEG (`.jpg`, `.jpeg`), PNG, WebP, BMP, TIFF (`.tif`, `.tiff`), GIF | JPEG (`.jpg`), PNG, WebP, BMP, TIFF (`.tiff`), GIF | Each input can be converted to any listed output. Each result is a still image. For GIF, TIFF, and WebP inputs, enable the first-frame or first-page option. |
| Create or merge PDFs | PDF and any of the supported image inputs above, in any combination | One PDF | The input list sets the page order. Each source PDF contributes all its pages at their original sizes. Images become JPEGs on A4 pages with 24 pt margins and automatic orientation. The first-frame or first-page option applies to GIF, TIFF, and WebP inputs. Forms, signatures, bookmarks, and document structure are outside the supported merge behavior. A PDF that requires a password causes the merge to fail. If an input fails, the combined PDF is withheld. |
| Export documents to PDF | DOCX, DOC, ODT, RTF, TXT | PDF | Uses installed Microsoft Word on Windows or LibreOffice Writer on macOS and Linux. Password-protected documents are unsupported. Layout depends on the Office backend and installed fonts. |
| Export spreadsheets to PDF | XLSX, XLS, ODS, CSV | PDF | Uses installed Microsoft Excel on Windows or LibreOffice Calc on macOS and Linux. The Office backend determines print areas and pagination. Sheet selection and print configuration are outside the 0.1 feature set. |

Video support depends on the codec inside the input container. Damaged files and unsupported codecs report an error. Video settings include quality (CRF 0–51), maximum width, frame rate, mute, and a size limit in MiB. Output dimensions are even. The size limit uses a separate bitrate calculation and checks the actual output size after encoding. It allows up to four attempts and fails without publishing a file if it cannot meet the limit at or above 64 kb/s. HDR is outside the validated format matrix.

Image resizing preserves the aspect ratio and only scales down. The quality setting applies to JPEG and WebP, with lower values giving higher quality. FFmpeg applies recognized orientation and strips metadata and ICC profiles. JPEG, BMP, and PDF put transparent areas on a white background. HEIF and AVIF are outside the 0.1 format list.

On Windows, Word and Excel exports use separate automation sessions. Files open read-only with macros, link updates, and recent-file tracking disabled. Availability is cached separately in `%LOCALAPPDATA%\Context Actions\capabilities.ini` after an attempt to start the respective app. **Check again (Office)** clears that cache. Cancel waits for an in-flight Office call to return. On macOS and Linux, LibreOffice exports run headless with a separate temporary user profile for each file. Cancel stops the conversion process.

Standalone format conversion for documents and spreadsheets is planned. The catalog shows those actions with their availability status.

## File operations

| Operation | What it does |
| --- | --- |
| Add, remove, or reorder inputs | Add files with the file picker, drag-and-drop, or command-line arguments. Removing a row only changes the input list. Move rows up or down to set PDF page order or the order used for rename numbering. |
| Preview and batch rename | Rename regular files in their existing folders while keeping their extensions. Use `{name}` and `{n}` in a pattern, choose a starting number, add a prefix or suffix, or replace text in the original base name. The app requires a preview before running and rejects conflicts, reserved names, swaps, and cycles. It attempts to roll back moves if a rename fails. |
| Undo the last rename | Restore the previous names if those names are free and the renamed files still match their recorded sizes and modification times. An interrupted rename can leave a `rename-pending.txt` journal with recovery paths. Resolve the journal before running another rename. Crash recovery is manual. |
| Save conversions | Choose the source folder or an output folder. Conversions leave source files intact. If an output name already exists, the app adds `(2)`, `(3)`, and so on. It writes to a temporary folder beside the destination and moves the verified output into place without overwriting an existing file. |
| Review or cancel a batch | Conversions run in sequence with overall progress and per-file status. An individual conversion failure leaves the remaining files queued. After a successful conversion, open the result or show it in the platform file manager. Canceling an FFmpeg or qpdf job stops the processes started for that job and removes its temporary files. |

For scripted processing, use `--headless` with `--report` and a new report filename. The app refuses to overwrite an existing report. Errors go to standard error if no report can be created. Reports may contain absolute paths and diagnostics. Exit code `0` means success, `1` means error, and `2` means cancellation.

The app uses shared English, Russian, German, French, and Simplified Chinese translations with an English fallback. macOS and Linux follow the system language. Windows also offers language and theme controls and a localized File Explorer menu.
