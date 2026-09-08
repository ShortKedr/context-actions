@echo off
setlocal EnableExtensions DisableDelayedExpansion

set "SCRIPT_DIR=%~dp0"
set "UI_SCRIPT=%SCRIPT_DIR%video-compress-ui.ps1"

set "SOURCE=%~1"
set "OUTPUT_DIR=%~2"
set "QUALITY=%~3"

set /a SUCCESS_COUNT=0
set /a SKIPPED_COUNT=0
set /a ERROR_COUNT=0
set /a DISCOVERED_COUNT=0

call :check_dependencies || exit /b 1
call :resolve_inputs
set "RESOLVE_EXIT=%ERRORLEVEL%"
if not "%RESOLVE_EXIT%"=="0" exit /b %RESOLVE_EXIT%
call :detect_encoder
call :configure_video_options || exit /b 1
call :ensure_output_dir || exit /b 1

echo(
echo Source: "%SOURCE%"
echo Output: "%OUTPUT_DIR%"
echo Quality: %QUALITY%
echo Video encoder: %VIDEO_ENCODER%
echo(

if /I "%SOURCE_KIND%"=="directory" (
    call :process_folder "%SOURCE%"
) else (
    call :process_single "%SOURCE%"
)

if %DISCOVERED_COUNT% equ 0 (
    echo [ERROR] No supported video files were found.
    exit /b 1
)

echo(
echo Summary: success=%SUCCESS_COUNT% skipped=%SKIPPED_COUNT% errors=%ERROR_COUNT%

if %ERROR_COUNT% gtr 0 exit /b 1
exit /b 0

:check_dependencies
where.exe ffmpeg >nul 2>nul || (
    echo [ERROR] ffmpeg was not found in PATH.
    exit /b 1
)
where.exe ffprobe >nul 2>nul || (
    echo [ERROR] ffprobe was not found in PATH.
    exit /b 1
)
exit /b 0

:resolve_inputs
if not defined SOURCE (
    call :run_ui
    set "UI_RESOLVE_EXIT=%ERRORLEVEL%"
    if not "%UI_RESOLVE_EXIT%"=="0" exit /b %UI_RESOLVE_EXIT%
)

if not defined SOURCE (
    echo [ERROR] Source path was not provided.
    exit /b 1
)

if not exist "%SOURCE%" (
    echo [ERROR] Source path does not exist: "%SOURCE%"
    exit /b 1
)

for %%I in ("%SOURCE%") do set "SOURCE_ATTR=%%~aI"
set "SOURCE_KIND=file"
if /I "%SOURCE_ATTR:~0,1%"=="d" set "SOURCE_KIND=directory"

if not defined OUTPUT_DIR (
    if /I "%SOURCE_KIND%"=="directory" (
        set "OUTPUT_DIR=%SOURCE%\converted-hevc"
    ) else (
        for %%I in ("%SOURCE%") do set "OUTPUT_DIR=%%~dpIconverted-hevc"
    )
)

if not defined QUALITY set "QUALITY=balanced"

if /I "%QUALITY%"=="high" (
    set "QUALITY=high"
    exit /b 0
)

if /I "%QUALITY%"=="balanced" (
    set "QUALITY=balanced"
    exit /b 0
)

if /I "%QUALITY%"=="compact" (
    set "QUALITY=compact"
    exit /b 0
)

echo [ERROR] Unsupported quality profile: "%QUALITY%"
echo         Allowed values: high, balanced, compact
exit /b 1

:run_ui
if not exist "%UI_SCRIPT%" (
    echo [ERROR] UI script was not found: "%UI_SCRIPT%"
    exit /b 1
)

set "UI_TEMP=%TEMP%\video-compress-ui-%RANDOM%%RANDOM%.txt"
if exist "%UI_TEMP%" del /q "%UI_TEMP%" >nul 2>nul

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%UI_SCRIPT%" -OutputFile "%UI_TEMP%"
set "UI_EXIT=%ERRORLEVEL%"

if not exist "%UI_TEMP%" (
    if "%UI_EXIT%"=="2" (
        echo [INFO] Selection was cancelled.
        exit /b 2
    )

    echo [ERROR] UI did not return any values.
    exit /b 1
)

for /f "usebackq tokens=1,* delims==" %%A in ("%UI_TEMP%") do (
    if /I "%%~A"=="source" set "SOURCE=%%~B"
    if /I "%%~A"=="output" set "OUTPUT_DIR=%%~B"
    if /I "%%~A"=="quality" set "QUALITY=%%~B"
)

del /q "%UI_TEMP%" >nul 2>nul

if "%UI_EXIT%"=="2" (
    echo [INFO] Selection was cancelled.
    exit /b 2
)

exit /b 0

:detect_encoder
ffmpeg -hide_banner -encoders 2>nul | findstr /I /C:"hevc_nvenc" >nul
if not errorlevel 1 (
    set "VIDEO_ENCODER=hevc_nvenc"
    exit /b 0
)

set "VIDEO_ENCODER=libx265"
exit /b 0

:configure_video_options
if /I "%VIDEO_ENCODER%"=="hevc_nvenc" (
    call :build_x265_fallback
    if "%QUALITY%"=="high" set "VIDEO_OPTS=-c:v hevc_nvenc -preset p7 -tune hq -rc vbr -cq 19 -b:v 0 -multipass fullres -spatial_aq 1 -aq-strength 8 -temporal_aq 1 -pix_fmt yuv420p -tag:v hvc1"
    if "%QUALITY%"=="balanced" set "VIDEO_OPTS=-c:v hevc_nvenc -preset p6 -tune hq -rc vbr -cq 23 -b:v 0 -multipass fullres -spatial_aq 1 -aq-strength 8 -temporal_aq 1 -pix_fmt yuv420p -tag:v hvc1"
    if "%QUALITY%"=="compact" set "VIDEO_OPTS=-c:v hevc_nvenc -preset p5 -tune hq -rc vbr -cq 28 -b:v 0 -multipass qres -spatial_aq 1 -aq-strength 10 -temporal_aq 1 -pix_fmt yuv420p -tag:v hvc1"
)

if /I "%VIDEO_ENCODER%"=="libx265" (
    call :build_x265_fallback
    set "VIDEO_OPTS=%X265_FALLBACK_OPTS%"
)

if defined VIDEO_OPTS exit /b 0

echo [ERROR] Could not build video encoder options.
exit /b 1

:build_x265_fallback
if "%QUALITY%"=="high" set "X265_FALLBACK_OPTS=-c:v libx265 -preset slow -crf 20 -pix_fmt yuv420p -tag:v hvc1"
if "%QUALITY%"=="balanced" set "X265_FALLBACK_OPTS=-c:v libx265 -preset medium -crf 24 -pix_fmt yuv420p -tag:v hvc1"
if "%QUALITY%"=="compact" set "X265_FALLBACK_OPTS=-c:v libx265 -preset medium -crf 28 -pix_fmt yuv420p -tag:v hvc1"
exit /b 0

:ensure_output_dir
if not exist "%OUTPUT_DIR%" goto make_output_dir

set "OUTPUT_ATTR="
for %%I in ("%OUTPUT_DIR%") do set "OUTPUT_ATTR=%%~aI"
if /I "%OUTPUT_ATTR:~0,1%"=="d" exit /b 0
echo [ERROR] Output path exists but is not a directory: "%OUTPUT_DIR%"
exit /b 1

:make_output_dir
mkdir "%OUTPUT_DIR%" >nul 2>nul || (
    echo [ERROR] Failed to create output directory: "%OUTPUT_DIR%"
    exit /b 1
)
exit /b 0

:process_folder
set "INPUT_DIR=%~1"

for %%F in ("%INPUT_DIR%\*.mp4") do if exist "%%~fF" call :process_file "%%~fF"
for %%F in ("%INPUT_DIR%\*.mkv") do if exist "%%~fF" call :process_file "%%~fF"
for %%F in ("%INPUT_DIR%\*.mov") do if exist "%%~fF" call :process_file "%%~fF"
for %%F in ("%INPUT_DIR%\*.avi") do if exist "%%~fF" call :process_file "%%~fF"
for %%F in ("%INPUT_DIR%\*.webm") do if exist "%%~fF" call :process_file "%%~fF"

exit /b 0

:process_single
call :process_file "%~1"
exit /b %errorlevel%

:process_file
setlocal DisableDelayedExpansion
set "INPUT_FILE=%~1"

if not exist "%INPUT_FILE%" (
    echo [ERROR] Missing input file: "%INPUT_FILE%"
    endlocal & set /a ERROR_COUNT+=1 & exit /b 1
)

for %%I in ("%INPUT_FILE%") do set "OUTPUT_FILE=%OUTPUT_DIR%\%%~nI_hevc_%QUALITY%.mp4"

if exist "%OUTPUT_FILE%" (
    echo [SKIP] "%INPUT_FILE%"
    echo        Output already exists: "%OUTPUT_FILE%"
    endlocal & (
        set /a DISCOVERED_COUNT+=1
        set /a SKIPPED_COUNT+=1
    ) & exit /b 0
)

set "AUDIO_CODEC="
for /f "usebackq delims=" %%A in (`ffprobe -v error -select_streams a:0 -show_entries stream^=codec_name -of default^=noprint_wrappers^=1:nokey^=1 -- "%INPUT_FILE%" 2^>nul`) do if not defined AUDIO_CODEC set "AUDIO_CODEC=%%A"

if /I "%AUDIO_CODEC%"=="aac" (
    set "AUDIO_OPTS=-c:a copy"
) else if defined AUDIO_CODEC (
    set "AUDIO_OPTS=-c:a aac -b:a 192k"
) else (
    set "AUDIO_OPTS=-an"
)

echo [INFO] "%INPUT_FILE%"
echo        ^> "%OUTPUT_FILE%"
echo        audio=%AUDIO_CODEC% quality=%QUALITY%

set "ACTIVE_VIDEO_OPTS=%VIDEO_OPTS%"
set "ACTIVE_ENCODER=%VIDEO_ENCODER%"

ffmpeg -hide_banner -y -i "%INPUT_FILE%" -map 0:v:0 -map 0:a:0? %ACTIVE_VIDEO_OPTS% %AUDIO_OPTS% -movflags +faststart -sn -dn "%OUTPUT_FILE%"
set "CONVERT_EXIT=%ERRORLEVEL%"

if not "%CONVERT_EXIT%"=="0" if /I "%VIDEO_ENCODER%"=="hevc_nvenc" (
    if exist "%OUTPUT_FILE%" del /q "%OUTPUT_FILE%" >nul 2>nul
    echo [WARN] NVENC failed, retrying with libx265 for "%INPUT_FILE%"
    set "ACTIVE_VIDEO_OPTS=%X265_FALLBACK_OPTS%"
    set "ACTIVE_ENCODER=libx265"
    ffmpeg -hide_banner -y -i "%INPUT_FILE%" -map 0:v:0 -map 0:a:0? %ACTIVE_VIDEO_OPTS% %AUDIO_OPTS% -movflags +faststart -sn -dn "%OUTPUT_FILE%"
    set "CONVERT_EXIT=%ERRORLEVEL%"
)

if not "%CONVERT_EXIT%"=="0" (
    if exist "%OUTPUT_FILE%" del /q "%OUTPUT_FILE%" >nul 2>nul
    echo [ERROR] Conversion failed for "%INPUT_FILE%"
    endlocal & (
        set /a DISCOVERED_COUNT+=1
        set /a ERROR_COUNT+=1
    ) & exit /b 1
)

echo [OK] Saved "%OUTPUT_FILE%" using %ACTIVE_ENCODER%
endlocal & (
    set /a DISCOVERED_COUNT+=1
    set /a SUCCESS_COUNT+=1
) & exit /b 0
