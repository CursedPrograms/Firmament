# Builds Firmament in C++ (Release) and dist\Firmament-win64.zip, through build.bat.
$ErrorActionPreference = 'Stop'
& cmd /c "`"$PSScriptRoot\build.bat`""
exit $LASTEXITCODE
