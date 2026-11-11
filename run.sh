#!/usr/bin/env bash
# Firmament: sets up its Python once, starts ComfyUI if needed, then serves http://127.0.0.1:7880
# (and on the home network: it prints a QR code for your phone).
set -e
cd "$(dirname "$0")"
if [ ! -x venv/bin/python ]; then
    echo "Setting up Firmament's Python, once..."
    python3 -m venv venv
    venv/bin/python -m pip install -r requirements.txt
fi
bash ./start_comfy.sh
exec venv/bin/python app.py
