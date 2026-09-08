@echo off
setlocal EnableExtensions

set "SCRIPT_PATH=%~dp0convert-video.cmd"
set "CMD_PATH=%ComSpec%"
set "MENU_KEY=HKCU\Software\Classes\SystemFileAssociations"
set "MENU_NAME=CompressWithFFmpeg"
set "MENU_TEXT=Compress video with FFmpeg"
set "MENU_ICON=imageres.dll,-5302"

if not exist "%SCRIPT_PATH%" (
    echo [ERROR] Script not found: "%SCRIPT_PATH%"
    exit /b 1
)

call :add_for_ext .mp4 || exit /b 1
call :add_for_ext .mkv || exit /b 1
call :add_for_ext .mov || exit /b 1
call :add_for_ext .avi || exit /b 1
call :add_for_ext .webm || exit /b 1

echo.
echo Context menu installed.
echo Supported file types: .mp4 .mkv .mov .avi .webm
echo.
echo Right-click a video file and choose "%MENU_TEXT%".
exit /b 0

:add_for_ext
set "EXT=%~1"
set "BASE_KEY=%MENU_KEY%\%EXT%\shell\%MENU_NAME%"

reg add "%BASE_KEY%" /ve /d "%MENU_TEXT%" /f >nul || exit /b 1
reg add "%BASE_KEY%" /v "Icon" /d "%MENU_ICON%" /f >nul || exit /b 1
reg add "%BASE_KEY%\command" /ve /d "\"%CMD_PATH%\" /d /c \"\"%SCRIPT_PATH%\" \"%%1\"\"" /f >nul || exit /b 1
exit /b 0
