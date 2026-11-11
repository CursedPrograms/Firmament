# Firmament: sets up its Python once, starts ComfyUI if needed, then serves http://127.0.0.1:7880
# (and on the home network: it prints a QR code for your phone).
Set-Location $PSScriptRoot
$py = Join-Path $PSScriptRoot "venv\Scripts\python.exe"
if (-not (Test-Path $py)) {
    Write-Host "Setting up Firmament's Python, once..."
    py -3.12 -m venv venv
    if ($LASTEXITCODE) { python -m venv venv }
    & $py -m pip install -r requirements.txt
    if ($LASTEXITCODE) { Write-Host "Setup failed, see above."; exit 1 }
}
& (Join-Path $PSScriptRoot "start_comfy.ps1")
& $py app.py
