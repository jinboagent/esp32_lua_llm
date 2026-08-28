@echo off
call C:\Espressif\idf_cmd_init.bat
cd /d E:\agent\esp32_lua_llm
rmdir /s /q build 2>nul
idf.py build
