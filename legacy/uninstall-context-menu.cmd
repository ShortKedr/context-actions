@echo off
setlocal EnableExtensions

set "MENU_KEY=HKCU\Software\Classes\SystemFileAssociations"
set "MENU_NAME=CompressWithFFmpeg"

call :remove_for_ext .mp4
call :remove_for_ext .mkv
call :remove_for_ext .mov
call :remove_for_ext .avi
call :remove_for_ext .webm

echo Context menu removed.
exit /b 0

:remove_for_ext
reg delete "%MENU_KEY%\%~1\shell\%MENU_NAME%" /f >nul 2>nul
exit /b 0
