@echo off
REM Send a command to the ESP32 via the IDF monitor's stdin
REM Usage: send_cmd.bat "SCAN START"
REM This writes to a temp file, then uses PowerShell to send it

set CMD=%~1
powershell -Command "[System.IO.Ports.SerialPort]::new('COM12', 115200) | ForEach-Object { $_.Open(); $_.WriteLine('%CMD%'); $_.Close() }" 2>nul

if errorlevel 1 (
    echo Failed to send via PowerShell. Try typing directly in the monitor.
)
