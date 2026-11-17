@echo off
setlocal
cd /d "%~dp0"
rem Installs Firmament for this user: %LOCALAPPDATA%\Programs\Firmament, with Start menu and Desktop shortcuts named Atelier - Firmament.
rem Run build.bat first (or unzip a release and run this beside it).
set "SRC=dist\Firmament"
if exist "Firmament.exe" set "SRC=."
if not exist "%SRC%\Firmament.exe" ( echo Build it first: build.bat & exit /b 1 )
tasklist /fi "imagename eq Firmament.exe" | find /i "Firmament.exe" >nul
if not errorlevel 1 ( echo Firmament is open: close it and run this again. & exit /b 1 )
set "INST=%LOCALAPPDATA%\Programs\Firmament"
if not exist "%INST%" mkdir "%INST%"
xcopy /e /i /q /y "%SRC%\*" "%INST%\" >nul || ( echo Couldn't copy to %INST% & exit /b 1 )
powershell -NoProfile -Command "$s=New-Object -ComObject WScript.Shell; foreach($d in @([Environment]::GetFolderPath('Programs'),[Environment]::GetFolderPath('Desktop'))){ Remove-Item (Join-Path $d 'Firmament.lnk') -Force -ErrorAction SilentlyContinue; $l=$s.CreateShortcut((Join-Path $d 'Atelier - Firmament.lnk')); $l.TargetPath='%INST%\Firmament.exe'; $l.WorkingDirectory='%INST%'; $l.IconLocation='%INST%\Firmament.exe,0'; $l.Description='Firmament: 360 skyboxes from a prompt'; $l.Save() }"
echo Installed: %INST%
if /i not "%~1"=="--quiet" echo Start it from the Start menu or the Desktop.
exit /b 0
