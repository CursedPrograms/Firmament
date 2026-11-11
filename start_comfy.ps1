# Start ComfyUI in its own window unless it already answers; called by run.ps1.
#   COMFYUI_DIR (default C:\cc\ComfyUI), COMFYUI_ARGS, COMFY_SERVER (skipped when it's another machine)
$dir = if ($env:COMFYUI_DIR) { $env:COMFYUI_DIR } else { "C:\cc\ComfyUI" }
$comfyArgs = if ($env:COMFYUI_ARGS) { $env:COMFYUI_ARGS } else { "--disable-comfy-compiler --disable-dynamic-vram --disable-async-offload --cache-ram 6 16" }
$server = if ($env:COMFY_SERVER) { $env:COMFY_SERVER } else { "http://127.0.0.1:8188" }
function Up { try { Invoke-WebRequest "$server/system_stats" -UseBasicParsing -TimeoutSec 3 | Out-Null; $true } catch { $false } }
if ($server -notmatch "127\.0\.0\.1|localhost") { Write-Host "Using ComfyUI at $server"; return }
if (Up) { Write-Host "ComfyUI is already running at $server"; return }
if (-not (Test-Path "$dir\main.py")) { Write-Host "ComfyUI isn't in ${dir}: set COMFYUI_DIR, or start it yourself."; return }
$py = if (Test-Path "$dir\venv\Scripts\python.exe") { "$dir\venv\Scripts\python.exe" } else { "python" }
Write-Host "Starting ComfyUI from $dir ..."
Start-Process cmd -ArgumentList "/k", "`"$py`" main.py $comfyArgs" -WorkingDirectory $dir
for ($i = 0; $i -lt 90; $i++) { Start-Sleep 2; if (Up) { Write-Host "ComfyUI is up."; return } }
Write-Host "ComfyUI didn't answer after 3 minutes: check its window."
