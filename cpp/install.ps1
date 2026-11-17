# Installs Firmament for this user (%LOCALAPPDATA%\Programs\Firmament, Start menu and Desktop shortcuts named "Atelier - Firmament"), through install.bat.
& cmd /c "`"$PSScriptRoot\install.bat`" $args"
exit $LASTEXITCODE
