@echo off
set /p PS4_IP="Enter your PS4 IP address to stop ps4-stream: "
powershell -Command "$c = New-Object System.Net.Sockets.TcpClient('%PS4_IP%', 5901); $c.Close()"
echo Stop signal sent to %PS4_IP%:5901.
pause
