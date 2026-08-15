@echo off
rem dev_env.bat — ensure the tmux "esp32" build session exists.
rem
rem The project workflow runs builds/flashes in a dedicated tmux pane so
rem long output streams in its own window instead of the agent/console
rem context: send commands with
rem     wsl -d Ubuntu tmux send-keys -t esp32:0.0 "<cmd> ; Write-Host SENTINEL" Enter
rem and poll for the sentinel (see README "Build workflow").
rem
rem Idempotent: safe to run on every session start (hooked via
rem .qwen/settings.json SessionStart).

wsl -d Ubuntu tmux has-session -t esp32 2>nul
if not errorlevel 1 (
    echo [dev_env] tmux session "esp32" already running
    goto :eof
)

for /f "delims=" %%P in ('wsl -d Ubuntu wslpath -a "%~dp0"') do set WSL_CWD=%%P
wsl -d Ubuntu tmux new-session -d -s esp32 -c "%WSL_CWD%"
rem Prefer the real PowerShell 7 binary. NEVER the WindowsApps pwsh.exe
rem shim: launching the shim from WSL interop kills the WSL instance and
rem takes the tmux server down with it. If pwsh is missing the pane stays
rem in bash (run builds via: cmd.exe /c build.bat).
wsl -d Ubuntu tmux send-keys -t esp32:0.0 "P='/mnt/c/Program Files/PowerShell/7/pwsh.exe'; if [ -x \"$P\" ]; then \"$P\" -NoLogo; fi" Enter
echo [dev_env] created tmux session "esp32"
echo [dev_env] attach with: wsl -d Ubuntu tmux attach -t esp32
