@echo off
set /p PS4_IP="Enter your PS4 IP address (e.g. 192.168.1.50): "
where mpv >nul 2>&1
if %errorlevel%==0 (
    mpv --profile=low-latency --untimed --no-cache http://%PS4_IP%:5900/stream.ts
    exit /b
)
where ffplay >nul 2>&1
if %errorlevel%==0 (
    ffplay -probesize 32 -analyzeduration 0 -fflags nobuffer+flush_packets -flags low_delay -framedrop -sync ext http://%PS4_IP%:5900/stream.ts
    exit /b
)
echo Please install mpv (winget install mpv) or FFmpeg (winget install FFmpeg).
pause
