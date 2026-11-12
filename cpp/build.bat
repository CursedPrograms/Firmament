@echo off
setlocal
cd /d "%~dp0"
rem Builds Firmament in C++ (Release) and dist\Firmament-win64.zip.
rem Needs Visual Studio 2022 (C++), CMake, the CUDA 12 toolkit, and stable-diffusion.cpp built with CUDA as a shared
rem library (see ..\README.md): SDCPP points at its folder.
if not defined SDCPP set "SDCPP=D:\cc\_src\stable-diffusion.cpp"
if not defined CUDA_BIN set "CUDA_BIN=%ProgramFiles%\NVIDIA GPU Computing Toolkit\CUDA\v12.9\bin"
if not defined BUILD_DIR set "BUILD_DIR=build"
if not exist "external\SDL2-2.30.9\cmake" (
    echo Getting SDL2's development package, once...
    if not exist external mkdir external
    curl -sL -o external\SDL2-devel.zip https://github.com/libsdl-org/SDL/releases/download/release-2.30.9/SDL2-devel-2.30.9-VC.zip || goto :error
    tar -xf external\SDL2-devel.zip -C external || goto :error
    del external\SDL2-devel.zip
)
if not exist "%SDCPP%\build-cuda\bin\stable-diffusion.dll" (
    echo stable-diffusion.cpp isn't built at %SDCPP%\build-cuda: see the README.
    goto :error
)
cmake -B %BUILD_DIR% -G "Visual Studio 17 2022" -A x64 -DSDCPP="%SDCPP%" || goto :error
cmake --build %BUILD_DIR% --config Release || goto :error
if exist dist\Firmament rmdir /s /q dist\Firmament
mkdir dist\Firmament
for %%f in (Firmament.exe SDL2.dll stable-diffusion.dll) do copy /y %BUILD_DIR%\Release\%%f dist\Firmament\ >nul || goto :error
xcopy /e /i /q /y %BUILD_DIR%\Release\assets dist\Firmament\assets >nul
rem NVIDIA's CUDA libraries the painter loads
for %%f in (cudart64_12.dll cublas64_12.dll cublasLt64_12.dll) do copy /y "%CUDA_BIN%\%%f" dist\Firmament\ >nul || goto :error
rem Microsoft's Visual C++ runtime and OpenMP, for stable-diffusion.dll (Firmament.exe has its own built in)
set "CRT="
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -find VC\Redist\MSVC\*\x64\Microsoft.VC143.CRT`) do set "CRT=%%i"
if not defined CRT echo Visual C++ runtime folder not found, is Visual Studio 2022 installed?& goto :error
xcopy /q /y "%CRT%\*.dll" dist\Firmament\ >nul || goto :error
for %%i in ("%CRT%\..\Microsoft.VC143.OpenMP\vcomp140.dll") do if exist %%i copy /y %%i dist\Firmament\ >nul
for %%f in (..\README.md ..\LICENSE ..\NOTICE install.bat install.ps1) do if exist %%f copy /y %%f dist\Firmament\ >nul
if exist dist\Firmament-win64.zip del dist\Firmament-win64.zip
"%SystemRoot%\System32\tar.exe" -a -c -f dist\Firmament-win64.zip -C dist Firmament || goto :error
echo Built: dist\Firmament-win64.zip
if exist "%LOCALAPPDATA%\Programs\Firmament\Firmament.exe" call "%~dp0install.bat" --quiet
exit /b 0
:error
echo Build failed, see above.
pause
exit /b 1
