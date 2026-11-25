#!/usr/bin/env bash
set -e
PS4_IP="${1:?Usage: ./watch.sh <PS4_IP>}"
if command -v mpv >/dev/null 2>&1; then
    exec mpv --profile=low-latency --untimed --no-cache "http://${PS4_IP}:5900/stream.ts"
elif command -v ffplay >/dev/null 2>&1; then
    exec ffplay -probesize 32 -analyzeduration 0 -fflags nobuffer+flush_packets -flags low_delay -framedrop -sync ext "http://${PS4_IP}:5900/stream.ts"
else
    echo "Error: Please install mpv or ffmpeg (ffplay)."
    exit 1
fi
