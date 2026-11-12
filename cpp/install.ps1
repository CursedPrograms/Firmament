# Installs Firmament for this user (%LOCALAPPDATA%\Programs\Firmament, Start menu + Desktop shortcuts), through install.bat.
& cmd /c "`"$PSScriptRoot\install.bat`" $args"
exit $LASTEXITCODE
