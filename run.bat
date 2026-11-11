@echo off
rem Firmament: sets up its Python once, starts ComfyUI if needed, then serves http://127.0.0.1:7880
rem (and on the home network: it prints a QR code for your phone).
setlocal
cd /d "%~dp0"
set "PY=%~dp0venv\Scripts\python.exe"
if not exist "%PY%" (
    echo Setting up Firmament's Python, once...
    py -3.12 -m venv venv 2>nul || python -m venv venv || goto :error
    "%PY%" -m pip install -r requirements.txt || goto :error
)
call "%~dp0start_comfy.bat"
"%PY%" app.py
pause
exit /b 0
:error
echo Setup failed, see above.
pause
exit /b 1
