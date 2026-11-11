#!/usr/bin/env bash
# Start ComfyUI in the background unless it already answers; called by run.sh.
#   COMFYUI_DIR (default ~/ComfyUI), COMFYUI_ARGS, COMFY_SERVER (skipped when it's another machine)
COMFYUI_DIR="${COMFYUI_DIR:-$HOME/ComfyUI}"
COMFYUI_ARGS="${COMFYUI_ARGS:---disable-comfy-compiler --disable-dynamic-vram --disable-async-offload --cache-ram 6 16}"
COMFY_SERVER="${COMFY_SERVER:-http://127.0.0.1:8188}"
up() { curl -s -o /dev/null "$COMFY_SERVER/system_stats"; }
case "$COMFY_SERVER" in *127.0.0.1*|*localhost*) ;; *) echo "Using ComfyUI at $COMFY_SERVER"; exit 0;; esac
up && { echo "ComfyUI is already running at $COMFY_SERVER"; exit 0; }
[ -f "$COMFYUI_DIR/main.py" ] || { echo "ComfyUI isn't in $COMFYUI_DIR: set COMFYUI_DIR, or start it yourself."; exit 0; }
PY=python3; [ -x "$COMFYUI_DIR/venv/bin/python" ] && PY="$COMFYUI_DIR/venv/bin/python"
echo "Starting ComfyUI from $COMFYUI_DIR (log: $COMFYUI_DIR/comfyui.log) ..."
(cd "$COMFYUI_DIR" && nohup "$PY" main.py $COMFYUI_ARGS > comfyui.log 2>&1 &)
for i in $(seq 1 90); do sleep 2; up && { echo "ComfyUI is up."; exit 0; }; done
echo "ComfyUI didn't answer after 3 minutes: check $COMFYUI_DIR/comfyui.log"
